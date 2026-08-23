export module fyuu_ui:pass_key;

export namespace fyuu_ui {

	template <class Owner>
	class PassKey {
		friend Owner;

		PassKey() noexcept = default;
		PassKey(PassKey const&) noexcept = default;
	};

}
