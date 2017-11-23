#include "notebook_window.h"

NotebookWindow::NotebookWindow() {
    set_title("juCi++ Notebook");
    add(Notebooks::get().create());
    show_all();
}
