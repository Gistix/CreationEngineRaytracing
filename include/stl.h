#pragma once

namespace stl
{
	using namespace SKSE::stl;

	template <class T, std::size_t Size = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();
		if (Size == 6) {
			T::func = *(uintptr_t*)trampoline.write_call<6>(a_src, T::thunk);
		}
		else {
			T::func = trampoline.write_call<Size>(a_src, T::thunk);
		}
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantID id)
	{
		REL::Relocation<std::uintptr_t> vtbl{ id };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantOffset offset)
	{
		REL::Relocation<std::uintptr_t> vtbl{ offset };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <class T>
	void write_thunk_jmp(std::uintptr_t a_src)
	{
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();
		T::func = trampoline.write_branch<5>(a_src, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}

	template <class T>
	void detour_thunk(REL::RelocationID a_relId)
	{
		T::func = a_relId.address();
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		DetourTransactionCommit();
	}

	template <class T>
	void detour_thunk_ignore_func(REL::RelocationID a_relId)
	{
		auto target = reinterpret_cast<PVOID>(a_relId.address());
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&target, reinterpret_cast<PVOID>(T::thunk));
		DetourTransactionCommit();
	}

	template <std::size_t idx, class T>
	void detour_vfunc(void* target)
	{
		auto vtable = *reinterpret_cast<uintptr_t**>(target);
		T::func = vtable[idx];
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
		DetourTransactionCommit();
	}
}