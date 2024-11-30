#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Funciones del sistema operativo
#include <fcntl.h>      // Operaciones con archivos
#include <errno.h>      // Gestión de errores del sistema
#include <sys/wait.h>   // Para manejar procesos hijos
#include "parser/parser.h" // Librería personalizada para analizar líneas de comandos

#define MAX_PATH 1024 // Tamaño máximo del buffer para rutas

// Declaración de funciones
void process_command(tline *cmd);       // Procesar un comando y ejecutarlo
void input_redirect(const char *file); // Configurar redirección de entrada
void output_redirect(const char *file);// Configurar redirección de salida
void run_cd(tline *cmd);               // Comando interno para cambiar directorio
void run_exit();                       // Comando interno para salir de la MiniShell

int main() {
    char input[1024];                  // Buffer para almacenar la entrada del usuario
    tline *parsed_line;                // Estructura para almacenar comandos analizados
    char current_dir[MAX_PATH];        // Buffer para la ruta del directorio actual

    while (1) { // Bucle infinito que mantiene viva la MiniShell
        // Obtener el directorio actual y manejar errores
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            perror("Error al obtener el directorio actual");
            exit(1);
        }

        // Mostrar el prompt con la ruta actual
        printf("%s> ", current_dir);

        // Leer la entrada del usuario
        if (!fgets(input, sizeof(input), stdin)) {
            break; // Salir si hay un error o EOF
        }

        // Parsear la línea introducida
        parsed_line = tokenize(input);
        if (parsed_line == NULL || parsed_line->ncommands == 0) {
            continue; // Ignorar líneas vacías
        }

        // Solo procesar un comando en esta implementación inicial
        if (parsed_line->ncommands == 1) {
            process_command(parsed_line);
        } else {
            fprintf(stderr, "Error: Solo se permite un comando por línea.\n");
        }
    }

    return 0;
}

// Procesar y ejecutar un comando
void process_command(tline *cmd) {
    pid_t child_pid;

    // Comprobar si es un comando interno (cd o exit)
    if (strcmp(cmd->commands[0].argv[0], "cd") == 0) {
        run_cd(cmd); // Cambiar directorio
        return;
    }

    if (strcmp(cmd->commands[0].argv[0], "exit") == 0) {
        run_exit(); // Salir de la MiniShell
        return;
    }

    // Crear un proceso hijo para ejecutar comandos externos
    child_pid = fork();
    if (child_pid < 0) { // Error en fork
        perror("Error al crear el proceso");
        exit(1);
    }
    if (child_pid == 0) { // Código que ejecuta el hijo
        if (cmd->redirect_input) { // Redirigir entrada si es necesario
            input_redirect(cmd->redirect_input);
        }
        if (cmd->redirect_output) { // Redirigir salida si es necesario
            output_redirect(cmd->redirect_output);
        }

        // Ejecutar el comando usando execvp
        execvp(cmd->commands[0].argv[0], cmd->commands[0].argv);

        // Si execvp falla, se notifica el error y se finaliza
        fprintf(stderr, "%s: Comando no encontrado\n", cmd->commands[0].argv[0]);
        exit(2);
    } else { // Código que ejecuta el padre
        waitpid(child_pid, NULL, 0); // Esperar a que termine el proceso hijo
    }
}

// Redirigir la entrada desde un archivo
void input_redirect(const char *file) {
    int fd = open(file, O_RDONLY); // Abrir el archivo en modo lectura (O_RDNLY indica SOLO EL MODO LECTURA, no permite más cosas)
    if (fd < 0) { // Manejar errores en la apertura del archivo
        fprintf(stderr, "%s: Error al abrir el archivo: %s\n", file, strerror(errno));
        exit(1);
    }

    // STDIN_FILENO es una constante definida en el encabezado <unistd.h> que representa el descriptor de archivo estándar para la entrada estándar (stdin)
    dup2(fd, STDIN_FILENO); // Redirigir entrada estándar
    close(fd); // Cerrar el archivo
}

// Redirigir la salida hacia un archivo
/*
*Modos de Apertura de Archivos:
O_WRONLY: Abre el archivo solo para escritura.
O_RDWR: Abre el archivo para lectura y escritura.
O_CREAT: Si el archivo no existe, lo crea.
O_APPEND: Abre el archivo en modo de anexado; es decir, los datos se agregan al final del archivo.
O_TRUNC: Si el archivo ya existe, se trunca a cero (se borra el contenido).
 *
 *
 */
void output_redirect(const char *file) {
    int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Abrir/crear archivo
    if (fd < 0) { // Manejar errores en la apertura del archivo
        fprintf(stderr, "%s: Error al abrir/crear el archivo: %s\n", file, strerror(errno));
        exit(1);
    }
    dup2(fd, STDOUT_FILENO); // Redirigir salida estándar
    close(fd); // Cerrar el archivo
}

// Implementación del comando 'cd'
void run_cd(tline *cmd) {
    const char *directory = NULL;

    // Si no se proporcionan argumentos, usar la variable HOME
    if (cmd->commands[0].argc == 1) {
        directory = getenv("HOME");
        if (directory == NULL) { // Manejar error si HOME no está definido
            fprintf(stderr, "Error: No se encontró la variable HOME\n");
            return;
        }
    } else {
        // Usar el primer argumento como ruta de destino
        directory = cmd->commands[0].argv[1];
    }

    // Cambiar el directorio actual
    if (chdir(directory) != 0) { // Manejar errores en chdir
        fprintf(stderr, "Error al cambiar de directorio: %s\n", strerror(errno));
    } else {
        // Mostrar la ruta actual tras el cambio
        char current_dir[MAX_PATH];
        printf("Directorio actual: %s\n", getcwd(current_dir, sizeof(current_dir)));
    }
}

// Implementación del comando 'exit'
void run_exit() {
    printf("Saliendo de la MiniShell...\n");
    exit(0); // Terminar el programa
}
