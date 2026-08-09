export module fyuu_editor;
import :application;

export namespace fyuu_editor {

	// Public C++ module entry. Called by main; forwards process arguments to the
	// internal application partition so consumers import only fyuu_editor.
	int Run(int argc, char** argv) {
		return RunApplication(argc, argv);
	}

}
