#pragma once

struct ShaderDefine {
	eastl::wstring name;
	eastl::wstring value;

	ShaderDefine(const wchar_t* name) : name(name)
	{
		this->value = L"";
	}

	ShaderDefine(const wchar_t* name, int value) : name(name)
	{
		// Workaround since eastl::to_string throws unresolved externals error
		std::wstring tmp = std::to_wstring(value);
		this->value.assign(tmp.data(), tmp.size());
	}

	ShaderDefine(const wchar_t* name, const wchar_t* value) : name(name), value(value) {};

	bool operator==(const ShaderDefine& other) const {
		return name == other.name && value == other.value;
	}
};