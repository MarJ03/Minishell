#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Funciones del sistema operativo
#include <fcntl.h>      // Operaciones con archivos
#include <errno.h>      // Gestión de errores del sistema
#include <stdbool.h>
#include <sys/wait.h>   // Para manejar procesos hijos
#include <sys/stat.h>  // Necesaria para la función umask
#include "parser/parser.h" // Librería personalizada para analizar líneas de comandos

#define MAX_PATH 1024 // Tamaño máximo del buffer para rutas
#define MAX_JOBS 20 //Número máximo de comandos simultáneos en ejecución en la minishell

// Declaración de funciones
void process_command(tline *cmd);      // Procesar un comando y ejecutarlo
void input_redirect(const char *file); // Configurar redirección de entrada
void output_redirect(const char *file);// Configurar redirección de salida
void run_cd(tline *cmd);               // Comando interno para cambiar directorio
void run_umask(tline *cmd);            // Comando interno umask
void run_exit();                       // Comando interno para salir de la MiniShell
void prompt_handler();                 // Impresión del prompt de la Minishell
void check_childs();                   // Comprueba el estado de los procesos hijos en segundo plano
void next_overwritable_job();           //Pendiente: Función para determinar el hueco en job_list del siguiente proceso a ejecutar


// Definir un enum para los estados de un trabajo
enum job_status {
    RUNNING = 1,
    STOPPED = 2,
    FINISHED = 3,
    ABORTED = 4
};

typedef struct job { //Cada elemento de tipo TJob se va a corresponder con una línea ejecutada
    pid_t pid;
    int job_id;
    tline* command; //Habrá que construir el comando del usuario leyendo del campo commands
    enum job_status status; //Estado del proceso
    bool shown; //Indica si el comando ha sido mostrado o no. Por defecto será true, pero para los procesos de background será false (para mostrar que ya han acabado en jobs)
} Tjob;



Tjob* job_list; //Estructura para almacenar la información de los comandos dentro de la minishell
int next_job = 0; //Siguiente posición libre de job_list
int job_list_size = 0; //Tamaño de la lista de jobs


int main() {
    char input[1024];                  // Buffer para almacenar la entrada del usuario
    tline *parsed_line;                // Estructura para almacenar comandos analizados

    signal(SIGINT, prompt_handler);    //Si llega la señal Ctrl+C ejecuta prompt_handler
    signal(SIGTSTP, SIG_IGN);          //Si llega la señal Ctrl+Z la ignora

    prompt_handler(); //Imprime el prompt

    job_list = (Tjob*) malloc(MAX_JOBS * sizeof(Tjob)); //Lista de comandos hijos (sin incluir a la propia minishell)
    for (int i = 0; i < MAX_JOBS; i++) {
        job_list[i].command = NULL;      // No hay comandos asignados al inicio
        job_list[i].status = FINISHED;  // Considerar todos los huecos como terminados inicialmente
        job_list[i].shown = true;       // Por defecto, mostrados
        job_list[i].pid = -1;           // Indicador de proceso inexistente
    }

    for(int i=0; i<MAX_JOBS; i++) {
        job_list[i].job_id = i+1;
    }

    while (1) { // Bucle infinito que mantiene viva la MiniShell

        //REVISAR (dan segmentation error)
        check_childs(); //Comprueba si algún hijo en segundo plano ha terminado
        next_overwritable_job(); //Encuentra el hueco dentro de la lista de jobs

        // Leer la entrada del usuario
        if (!fgets(input, sizeof(input), stdin)) {
            break; // Salir si hay un error o EOF
        }

        // Parsear la línea introducida
        parsed_line = tokenize(input);
        if (parsed_line == NULL || parsed_line->ncommands == 0) {
            continue; // Ignorar líneas vacías
        }

        /*
        //Auxiliar
        for(int i=0; i< parsed_line-> ncommands; i++) {
            printf(parsed_line->commands[i].argv[0]);
        }
        */

        //Añade la instrucción introducida por el usuario a job_list
        job_list[next_job].command = parsed_line;

        // Solo procesar un comando en esta implementación inicial
        process_command(parsed_line);
        prompt_handler();
    }

    return 0;
}

void process_command(tline *cmd) {

    // Comprobar si es un comando interno (cd, exit o umask)
    if (strcmp(cmd->commands[0].argv[0], "cd") == 0) {
        run_cd(cmd); // Cambiar directorio
        return;
    }

    if (strcmp(cmd->commands[0].argv[0], "exit") == 0) {
        run_exit(); // Salir de la MiniShell
        return;
    }

    if (strcmp(cmd->commands[0].argv[0], "umask") == 0) {
        run_umask(cmd); // Salir de la MiniShell
        return;
    }

    // Si es un comando externo
    int **pipe_array = (int **)malloc((cmd->ncommands - 1) * sizeof(int *));
    for (int i = 0; i < cmd->ncommands - 1; i++) {
        pipe_array[i] = (int *)malloc(2 * sizeof(int));
        if (pipe(pipe_array[i]) < 0) {
            perror("Error creando la pipe");
            exit(1);
        }
    }

    for (int j = 0; j < cmd->ncommands; j++) {
        const pid_t pid = fork();
        if (pid < 0) {
            perror("Error al crear el proceso");
            exit(1);
        }

        if (pid == 0) { // Proceso hijo

            //Si es un proceso de background, ignora la señal Ctrl+C
            if(cmd->background == 1) {
                signal(SIGINT, SIG_DFL); //Pendiente de probar cuando haya procesos en background
            }

            if (j > 0) {
                // Si no es el primer comando, redirigir entrada desde la pipe anterior
                dup2(pipe_array[j - 1][0], STDIN_FILENO);
            }

            if (j < cmd->ncommands - 1) {
                // Si no es el último comando, redirigir salida hacia la siguiente pipe
                dup2(pipe_array[j][1], STDOUT_FILENO);
            }

            // Si es el primer comando y se requiere redirigir la entrada
            if (j == 0 && cmd->redirect_input) {
                input_redirect(cmd->redirect_input);
            }

            // Si es el último comando y se requiere redirigir la salida
            if (j == cmd->ncommands - 1 && cmd->redirect_output) {
                output_redirect(cmd->redirect_output);
            }

            // Cerrar todas las pipes que no se estén utilizando
            for (int i = 0; i < cmd->ncommands - 1; i++) {
                close(pipe_array[i][0]);
                close(pipe_array[i][1]);
            }

            // Ejecutar el comando
            execvp(cmd->commands[j].argv[0], cmd->commands[j].argv);

            // Si execvp falla
            perror("Error al ejecutar el comando");
            exit(1);
        }
    }

    // Cerrar todas las pipes en el padre
    for (int i = 0; i < cmd->ncommands - 1; i++) {
        close(pipe_array[i][0]);
        close(pipe_array[i][1]);
        free(pipe_array[i]);
    }
    free(pipe_array);

    // Esperar a que todos los hijos terminen
    for (int j = 0; j < cmd->ncommands; j++) {
        if(cmd->background == 0) { //Únicamente espera por los hijos que son procesos en foreground
            wait(NULL);
        }
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

//Implementacion del comando 'umask'
void run_umask(tline *cmd) {
    // Verificar si se proporcionó un argumento para umask
    if (cmd->commands[0].argc == 1) {
        // Si no se proporciona argumento, simplemente mostramos el valor actual de umask
        mode_t current_umask = umask(0);  // Esto "lee" el umask actual y lo restablece
        printf("Valor de máscara (umask) actual: %03o\n", current_umask);  // Muestra el valor en formato octal con 3 dígitos
        umask(current_umask);  // Restablecer la máscara al valor original
    } else {

        // necesitamos fincadena para saber si la conversión a octal se completa, o por lo contrario, no lo hace, pudiendo
        // quedar caracteres no válidos después del número. Ejemplo: 22b--> al apuntar a b, se dará cuenta de que la
        //conversión no está bien hecha, y no pasará un valor no válido a umask. Se pasa como & porque strtol necesita
        // modificar la cadena para la tranformación, aunque se quede a la mitad
        char *fincadena;
        // Si se proporciona un argumento, validamos que sea un valor octal válido
        const char *umask_value_str = cmd->commands[0].argv[1];


        // Convertir el valor a un número entero
        long umask_value = strtol(umask_value_str, &fincadena, 8);  // Base 8 para octal

        // Verificar si la conversión fue exitosa
        if (*fincadena != '\0' || umask_value < 0 || umask_value > 0777) {
            // Si no es un número octal válido, mostrar un mensaje de error
            fprintf(stderr, "%s: Valor de umask inválido\n", umask_value_str);
            return;
        }

        // Establecer la nueva umask
        mode_t new_umask = (mode_t)umask_value;
        umask(new_umask);  // Cambiar la máscara de umask
        printf("Máscara (umask) cambiada a: %o\n", new_umask);  // Mostrar el nuevo valor en octal
    }
}

// Implementación del comando 'exit'
void run_exit() {
    printf("Saliendo de la MiniShell...\n");
    exit(0); // Terminar el programa
}

void prompt_handler() {
    char current_dir[MAX_PATH];        // Buffer para la ruta del directorio actual
    // Obtener el directorio actual y manejar errores
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("Error al obtener el directorio actual");
    }

    // Mostrar el prompt con la ruta actual
    printf("\n%s> ", current_dir);

    fflush(stdout); //Limpia el buffer intermedio de la salida estándar
}

void check_childs() {
    int exit_status;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].command != NULL && job_list[i].command->background) {
            pid_t pid = waitpid(job_list[i].pid, &exit_status, WNOHANG);

            if (pid > 0) {
                if (WIFEXITED(exit_status) || WIFSIGNALED(exit_status)) {
                    job_list[i].status = FINISHED; // Marcar como terminado
                    job_list[i].shown = false;    // Mostrar el estado terminado
                    free(job_list[i].command);   // Liberar memoria si se asignó dinámicamente
                    job_list[i].command = NULL;  // Marcar el hueco como vacío
                }
            } else if (pid < 0) {
                perror("Error al comprobar estado de hijos");
            }
        }
    }
}

void next_overwritable_job() {
    for (int i = 0; i < MAX_JOBS; i++) {
        // Si el hueco está vacío o es un proceso en background mostrado, es válido
        if (job_list[i].command == NULL ||
            (job_list[i].command->background && job_list[i].shown)) {
            next_job = i;
            return; // Salir tras encontrar el primer hueco válido
            }
    }

    // Si no se encontró un hueco válido, manejar el error
    fprintf(stderr, "Error: No se encontró un hueco válido en job_list\n");


}
