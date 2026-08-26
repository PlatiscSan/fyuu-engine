export module fyuu_ui:pass_key;

export namespace fyuu_ui {

	/// Restricts a public operation to `Owner` without granting friendship to the
	/// complete receiving class. Only Owner can construct the otherwise empty key.
	template <class Owner>
	class PassKey {
		friend Owner;

		PassKey() noexcept = default;

	public:
		PassKey(PassKey const&) noexcept = default;
		PassKey& operator=(PassKey const&) noexcept = default;
	};

}
