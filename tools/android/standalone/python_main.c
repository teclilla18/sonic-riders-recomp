/* Match the existing tools' command-line Python entry points. */
extern int Py_BytesMain(int argc, char **argv);

int main(int argc, char **argv) {
    return Py_BytesMain(argc, argv);
}
