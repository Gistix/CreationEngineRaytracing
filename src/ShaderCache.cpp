#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Util.h"
#include "Renderer.h"
#include <dxcapi.h>
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <vector>

#include <mutex>

namespace ShaderCache
{
	eastl::unordered_map<ShaderKey, winrt::com_ptr<IDxcBlob>, ShaderKeyHash> m_Shaders;
	std::mutex m_ShadersMutex;

	static constexpr uint32_t kCacheFileMagic = 0x44485343;  // 'DHSC'
	static constexpr uint32_t kCacheVersion = 2;

	static uint64_t HashBuffer64(const void* data, size_t size)
	{
		const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
		uint64_t hash = 14695981039346656037ULL;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= static_cast<uint64_t>(ptr[i]);
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	// Produces a fixed-width 64-bit hash of the ShaderKey for use in disk cache
	// filenames, avoiding the platform-dependent width of size_t.
	static uint64_t ComputeDiskCacheKeyHash(const ShaderKey& key)
	{
		uint64_t hash = 14695981039346656037ULL;
		auto hashBytes = [&hash](const void* data, size_t size) {
			const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; ++i) {
				hash ^= static_cast<uint64_t>(ptr[i]);
				hash *= 1099511628211ULL;
			}
		};

		auto hashString = [&hash, &hashBytes](const wchar_t* str, size_t len) {
			hashBytes(str, len * sizeof(wchar_t));
			wchar_t nullChar = 0;
			hashBytes(&nullChar, sizeof(wchar_t));
		};

		hashString(key.filePath.data(), key.filePath.size());
		hashString(key.target.data(), key.target.size());
		hashString(key.entryPoint.data(), key.entryPoint.size());
		uint8_t vulkan = key.isVulkan ? 1 : 0;
		hashBytes(&vulkan, 1);
		for (const auto& d : key.defines) {
			hashString(d.name.data(), d.name.size());
			hashString(d.value.data(), d.value.size());
		}
		return hash;
	}

	// Hash of compile-time build configuration that affects bytecode output.
	// Stored in the cache header so debug/release builds don't share entries.
	static uint64_t ComputeConfigHash()
	{
		uint64_t hash = 14695981039346656037ULL;
#ifdef NDEBUG
		hash ^= 0x52454C45415345ULL;  // "RELEASE"
#else
		hash ^= 0x4445425547ULL;       // "DEBUG"
#endif
		hash *= 1099511628211ULL;
		return hash;
	}

#pragma pack(push, 1)
	struct CacheHeader {
		uint32_t magic;
		uint32_t version;
		uint64_t preprocessedHash;
		uint64_t configHash;
		uint32_t bytecodeSize;
	};
#pragma pack(pop)

	static winrt::com_ptr<IDxcBlob> LoadFromDiskCache(const std::filesystem::path& cacheFilePath, uint64_t currentPreprocessedHash, uint64_t currentConfigHash)
	{
		std::ifstream file(cacheFilePath, std::ios::binary);
		if (!file.is_open()) return nullptr;

		CacheHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(CacheHeader));
		if (!file) {
			logger::warn("ShaderCache - Failed to read cache header from {}", cacheFilePath.string());
			return nullptr;
		}
		if (header.magic != kCacheFileMagic || header.version != kCacheVersion) {
			logger::warn("ShaderCache - Invalid cache header (magic {:08x}, version {}) in {}", header.magic, header.version, cacheFilePath.string());
			return nullptr;
		}

		if (header.preprocessedHash != currentPreprocessedHash) {
			logger::debug("ShaderCache - Disk cache hash mismatch for {}", cacheFilePath.string());
			return nullptr;
		}

		if (header.configHash != currentConfigHash) {
			logger::debug("ShaderCache - Disk cache config mismatch for {}", cacheFilePath.string());
			return nullptr;
		}

		if (header.bytecodeSize == 0) {
			logger::error("ShaderCache - Empty bytecode in {}", cacheFilePath.string());
			return nullptr;
		}

		std::vector<char> buffer(header.bytecodeSize);
		file.read(buffer.data(), header.bytecodeSize);
		if (!file) {
			logger::error("ShaderCache - Failed to read binary payload from {}", cacheFilePath.string());
			return nullptr;
		}

		auto dxc = ShaderUtils::DirectXShaderCompiler::GetSingleton();
		winrt::com_ptr<IDxcBlobEncoding> blobEncoding;
		// Binary shader bytecode — DXC_CP_UTF8 is used as a byte-transparent
		// encoding since CreateBlob does not perform encoding conversion.
		if (FAILED(dxc->utils->CreateBlob(buffer.data(), static_cast<uint32_t>(buffer.size()), DXC_CP_UTF8, blobEncoding.put()))) {
			logger::error("ShaderCache - CreateBlob failed for {}", cacheFilePath.string());
			return nullptr;
		}

		// copy_from performs a ref-count bump, not a deep copy.  This is safe
		// because CreateBlob (without PINNED) copies buffer's data internally.
		// If this is ever changed to a pinned blob, the local buffer would need
		// to outlive the blob.
		winrt::com_ptr<IDxcBlob> blob;
		blob.copy_from(blobEncoding.get());
		return blob;
	}

	static void SaveToDiskCache(const std::filesystem::path& cacheFilePath, uint64_t currentPreprocessedHash, uint64_t currentConfigHash, IDxcBlob* blob)
	{
		if (!blob) return;

		std::error_code ec;
		std::filesystem::create_directories(cacheFilePath.parent_path(), ec);
		if (ec) {
			logger::warn("ShaderCache - Failed to create cache directory {}: {}", cacheFilePath.parent_path().string(), ec.message());
			return;
		}

		// Write to a temporary file first, then rename atomically to prevent
		// partially-written cache files from persisting after a crash.
		std::filesystem::path tempPath = cacheFilePath;
		tempPath += L".tmp";

		{
			std::ofstream file(tempPath, std::ios::binary);
			if (!file.is_open()) {
				logger::error("ShaderCache - Failed to open {} for writing", tempPath.string());
				return;
			}

			CacheHeader header{
				.magic = kCacheFileMagic,
				.version = kCacheVersion,
				.preprocessedHash = currentPreprocessedHash,
				.configHash = currentConfigHash,
				.bytecodeSize = static_cast<uint32_t>(blob->GetBufferSize())
			};

			file.write(reinterpret_cast<const char*>(&header), sizeof(CacheHeader));
			file.write(reinterpret_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());

			if (!file) {
				logger::error("ShaderCache - Failed to write shader data to {}", tempPath.string());
				file.close();
				std::error_code ignore;
				std::filesystem::remove(tempPath, ignore);
				return;
			}
		}  // ofstream closes here before rename

		std::filesystem::rename(tempPath, cacheFilePath, ec);
		if (ec) {
			logger::error("ShaderCache - Failed to rename temp cache file: {}", ec.message());
			std::filesystem::remove(tempPath, ec);
			return;
		}

		logger::debug("ShaderCache - Saved shader binary to disk cache: {}", cacheFilePath.string());
	}

	winrt::com_ptr<IDxcBlob> GetShader(const wchar_t* filePath, eastl::vector<DxcDefine> defines, ShaderStage stage, const wchar_t* entryPoint)
	{
		const wchar_t* target = Renderer::GetSingleton()->GetShaderTarget(stage);
		return GetShader(filePath, std::move(defines), target, entryPoint);
	}

	winrt::com_ptr<IDxcBlob> GetShader(const wchar_t* filePath, eastl::vector<DxcDefine> defines, const wchar_t* target, const wchar_t* entryPoint)
	{
		bool isVulkan = Renderer::GetSingleton()->IsVulkan();
		ShaderKey shaderKey(filePath, defines, target, entryPoint, isVulkan);

		// Return in-memory cached shader
		{
			std::lock_guard lock(m_ShadersMutex);
			if (auto it = m_Shaders.find(shaderKey); it != m_Shaders.end()) {
				logger::debug("ShaderCache::GetShader - Returning in-memory cached shader");
				return it->second;
			}
		}

		// TODO: Consider adding cache eviction or periodic cleanup of stale entries
		// to prevent unbounded growth of the disk cache directory.

		std::filesystem::path cacheDir = std::string(PLUGIN_FOLDER) + "/CreationEngineRaytracing/ShaderCache";

		std::filesystem::path hlslPath(filePath);
		// Build the cache filename entirely in wide characters. Narrowing the
		// stem via path::string() would use the active code page, mangling
		// non-ASCII shader paths into '?' characters (invalid in Windows
		// filenames) and varying per locale.
		uint64_t keyHash = ComputeDiskCacheKeyHash(shaderKey);
		std::filesystem::path cacheFilePath = cacheDir / std::format(L"{}_{:016x}.bin", hlslPath.stem().wstring(), keyHash);
		uint64_t configHash = ComputeConfigHash();

		// Preprocess shader with DXC -P to obtain preprocessed text and compute hash.
		// defines is passed by copy intentionally: PreprocessShader mutates its copy,
		// and we may still need the original defines for fallback compilation below.
		winrt::com_ptr<IDxcBlobUtf8> preprocessedText;
		uint64_t preprocessedHash = 0;
		bool preprocessedOk = ShaderUtils::PreprocessShader(preprocessedText, filePath, defines, target, entryPoint);

		if (preprocessedOk && preprocessedText && preprocessedText->GetStringLength() > 0) {
			preprocessedHash = HashBuffer64(preprocessedText->GetStringPointer(), preprocessedText->GetStringLength());

			// Try loading from disk cache
			winrt::com_ptr<IDxcBlob> diskBlob = LoadFromDiskCache(cacheFilePath, preprocessedHash, configHash);
			if (diskBlob) {
				logger::debug("ShaderCache::GetShader - Returning disk-cached shader: {}", cacheFilePath.string());
				std::lock_guard lock(m_ShadersMutex);
				auto [it, emplaced] = m_Shaders.try_emplace(shaderKey, diskBlob);
				return it->second;
			}
			logger::debug("ShaderCache::GetShader - Disk cache miss for {}", cacheFilePath.string());
		} else {
			logger::warn("ShaderCache::GetShader - Preprocessing failed for {}, falling back to raw compilation (no disk caching)", Util::WStringToString(filePath));
		}

		// Disk cache miss: compile shader
		winrt::com_ptr<IDxcBlob> blob;

		if (preprocessedOk && preprocessedText && preprocessedText->GetStringLength() > 0) {
			// Compile directly from preprocessed text to avoid redundant preprocessing
			ShaderUtils::CompilePreprocessedShader(blob, preprocessedText->GetStringPointer(), preprocessedText->GetStringLength(), filePath, target, entryPoint);
		} else {
			// Preprocessing failed: fall back to full compilation (no disk caching)
			ShaderUtils::CompileRawShader(blob, filePath, std::move(defines), target, entryPoint);
		}

		if (!blob) {
			logger::error("ShaderCache::GetShader - Compilation failed for {}", Util::WStringToString(filePath));
			return nullptr;
		}

		// Save compiled binary to disk cache
		if (preprocessedOk) {
			SaveToDiskCache(cacheFilePath, preprocessedHash, configHash, blob.get());
		}

		// Save shader to in-memory cache
		std::lock_guard lock(m_ShadersMutex);
		auto [it, emplaced] = m_Shaders.try_emplace(shaderKey, blob);

		if (!emplaced) {
			logger::error("ShaderCache::GetShader - Emplace failed.");
			return nullptr;
		}

		logger::debug("ShaderCache::GetShader - Returning newly compiled shader");
		return it->second;
	}
};