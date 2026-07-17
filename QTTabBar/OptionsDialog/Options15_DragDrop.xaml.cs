namespace QTTabBarLib {
    internal partial class Options15_DragDrop : OptionsDialogTab {

        public Options15_DragDrop() {
            WpfResourceLoader.LoadComponent(this, "optionsdialog/options15_dragdrop.xaml");
        }

        public override void InitializeConfig() {
            // Bindings keep the page synchronized with WorkingConfig.
        }

        public override void ResetConfig() {
            DataContext = WorkingConfig.dragdrop = new Config._DragDrop();
        }

        public override void CommitConfig() {
            // Bindings keep the page synchronized with WorkingConfig.
        }
    }
}
