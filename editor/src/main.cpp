import fyuu_editor;

// Native process entry -> fyuu_editor::Run -> RunApplication -> EditorApplication::Run
// -> Fyuu_Run. Keeping this file trivial also permits alternative launch hosts later.
int main(int argc, char** argv) {
	return fyuu_editor::Run(argc, argv);
}
