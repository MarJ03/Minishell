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
#define MAX_LINE_LENGTH 1024

// Definir un enum para los estados de un trabajo
enum job_status {
    RUNNING = 1,  //En ejecución
    SUSPENDED = 2,  //Suspendido
    FINISHED = 3 //Terminado normalmente
};

typedef struct job { //Cada elemento de tipo TJob se va a corresponder con una línea ejecutada
    int ncommands;
    pid_t* pid_array;
    int job_id;
    char* command; //Habrá que construir el comando del usuario leyendo del campo commands
    enum job_status status; //Estado del proceso
    bool shown; //Indica si el comando ha sido mostrado o no. Por defecto será true, pero para los procesos de background será false (para mostrar que ya han acabado en jobs)
} Tjob;


Tjob* job_list; //Estructura para almacenar la información de los comandos dentro de la minishell
int next_job = 0; //Siguiente posición libre de job_list
int last_job = -1; //Posición del último job almacenado
int prev_last_job = -1; //Posición del penúltimo job almacenado
tline *parsed_line; // Estructura para almacenar comandos analizados
Tjob fg_job; //Información del único proceso en ejecución en foreground


// Declaración de funciones
void process_command(tline *cmd);      // Procesar un comando y ejecutarlo
void input_redirect(const char *file); // Configurar redirección de entrada
void output_redirect(const char *file);// Configurar redirección de salida
void run_cd(tline *cmd);               // Comando interno para cambiar directorio
void run_umask(tline *cmd);            // Comando interno umask
void run_exit();                       // Comando interno para salir de la MiniShell
void prompt_handler();                 // Impresión del prompt de la Minishell
void stop_handler();                   // Cuando se detecta Ctrl+Z, cambia el estado del único job en foreground a "Suspendido" y muestra por pantalla su estado actual
void check_jobs();                     // Comprueba para todos los jobs si todos los procesos que componen un job han terminado, y renombra los job_id en función de los elementos de job_list
void fill_job(Tjob* job, tline* parsed_line);              // Rellena el job requerido con sus campos correspondientes
void free_job(Tjob* job);              // Restaura los datos de un job a los datos por defecto
void next_overwritable_job();          // Pendiente: Función para determinar el hueco en job_list del siguiente proceso a ejecutar


int main() {
    char input[1024];                  // Buffer para almacenar la entrada del usuario

    signal(SIGINT, prompt_handler);    //Si llega la señal Ctrl+C ejecuta prompt_handler
    signal(SIGTSTP, stop_handler);          //Si llega la señal Ctrl+Z la ignora

    prompt_handler(); //Imprime el prompt

    job_list = (Tjob*) malloc(MAX_JOBS * sizeof(Tjob)); //Lista de comandos hijos (sin incluir a la propia minishell)

    //Inicializamos todos los jobs de job_list
    for (int i = 0; i < MAX_JOBS; i++) {
        job_list[i].ncommands = 0;
        job_list[i].command = (char*)malloc(MAX_LINE_LENGTH * sizeof(char)); //Texto vacío en el que irá la línea introducida
        job_list[i].status = FINISHED;  // Considerar todos los huecos como terminados inicialmente
        job_list[i].shown = true;       // Por defecto, mostrados
        job_list[i].pid_array = NULL;           // Indicador de proceso inexistente
    }

    //Inicializamos el único job en foreground
    fg_job.ncommands = 0;
    fg_job.command = (char*)malloc(MAX_LINE_LENGTH * sizeof(char)); //Texto vacío en el que irá la línea introducida
    fg_job.status = FINISHED;  // Considerar todos los huecos como terminados inicialmente
    fg_job.shown = true;       // Por defecto, mostrados
    fg_job.pid_array = NULL;           // Indicador de proceso inexistente

    for(int i=0; i<MAX_JOBS; i++) {
        job_list[i].job_id = i+1;
    }

    while (1) { // Bucle infinito que mantiene viva la MiniShell

        check_jobs();
        next_overwritable_job(); //Encuentra el hueco dentro de la lista de jobs

        // Leer la entrada del usuario
        if (!fgets(input, sizeof(input), stdin)) {
            break; // Salir si hay un error o EOF
        }

        // Parsear la línea introducida
        parsed_line = tokenize(input);
        if (parsed_line != NULL && parsed_line->ncommands > 0) {
            //Crea la línea introducida a partir del tline
            if(parsed_line->background) {
                fill_job(&job_list[next_job], parsed_line);
            }
            else {
                free_job(&fg_job);
                fill_job(&fg_job, parsed_line);
            }

            // Solo procesar un comando en esta implementación inicial
            process_command(parsed_line);

            if(parsed_line->background) {
                prev_last_job = last_job;
                last_job = next_job;
            }
        }

        prompt_handler();
    }

    return 0;
}

void process_command(tline *cmd) {
    // Verificar si es un comando interno (cd, exit, umask, etc.)
    if (strcmp(cmd->commands[0].argv[0], "cd") == 0) {
        run_cd(cmd);
        return;
    }
    if (strcmp(cmd->commands[0].argv[0], "exit") == 0) {
        run_exit();
        return;
    }
    if (strcmp(cmd->commands[0].argv[0], "umask") == 0) {
        run_umask(cmd);
        return;
    }

    if (strcmp(cmd->commands[0].argv[0], "bg") == 0) {
        int selected_job = atoi(cmd->commands[0].argv[1]);
        if(selected_job >= 1 && selected_job <= MAX_JOBS) {
            for(int i=0; i<MAX_JOBS; i++) {
                if(job_list[i].job_id == selected_job) {

                    if(job_list[i].status == SUSPENDED) { //Está parado y le llega bg
                        for(int j=0; j<job_list[i].ncommands; j++) {
                            kill(job_list[i].pid_array[j], SIGCONT);
                        }
                        strcat(job_list[i].command, "&");
                        job_list[i].status = RUNNING;
                        printf("[%d]+ %s", job_list[i].job_id, job_list[i].command);

                        prev_last_job = last_job; //Actualizamos el último comando para que salga con el símbolo correcto al ejecutar jobs
                        last_job = next_job;
                        return;
                    }

                    else if(job_list[i].status == FINISHED && job_list[i].ncommands > 0) { //Está terminado y le llega bg
                        printf("bg: el trabajo %d ya ha terminado", selected_job);
                        return;
                    }

                    else if(job_list[i].status == RUNNING){ //Está en ejecución y le llega bg
                        printf("bg: el trabajo %d ya está en segundo plano", selected_job);
                        return;
                    }
                }
            }
        }

        printf("bg: el trabajo %d no existe", selected_job); //Si no ha salido de la ejecución hasta ahora, es que el job introducido no existe
        return;
    }

    if (strcmp(cmd->commands[0].argv[0], "jobs") == 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if(strcmp(job_list[i].command, "") != 0 && job_list[i].shown == false) {
                char* job_status = "";
                switch(job_list[i].status) {
                    case RUNNING:
                        job_status = "Running";
                    break;
                    case SUSPENDED:
                        job_status = "Stopped";
                    break;
                    case FINISHED:
                        job_status = "Done";
                    break;
                }

                printf("[%d]", job_list[i].job_id);

                if(job_list[last_job].status == FINISHED && job_list[last_job].shown == true) {
                    if(job_list[i].job_id - 1 == prev_last_job) { //-1 porque job_id empieza por 1 y las posiciones de job_list empiezan por 0
                        printf("+  ");
                    }
                    else printf("-  ");
                }
                else {
                    if(job_list[i].job_id - 1 == last_job) { //-1 porque job_id empieza por 1 y las posiciones de job_list empiezan por 0
                        printf("+  ");
                    }
                    else printf("-  ");
                }

                printf("%s \t\t\t %s\n", job_status, job_list[i].command);

                //Ya no se va a tener que mostrar de nuevo, pues ya se ha mostrado una vez como hecho
                if(job_list[i].status == FINISHED) { //Solamente se ejecutará en el caso de que shown sea false y el status sea FINISHED, lo que indicará que acaba de terminar y su posición en job_list debe ser vaciada
                    free_job(&job_list[i]);
                }
            }
        }
        return;
    }

    //Opción extra para mostrar el contenido de job_list
    if (strcmp(cmd->commands[0].argv[0], "debug") == 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            char* job_status = "";
            switch(job_list[i].status) {
                case RUNNING:
                    job_status = "Running";
                    break;
                case SUSPENDED:
                    job_status = "Stopped";
                    break;
                case FINISHED:
                    job_status = "Done";
                    break;
            }

            printf("PIDs: [");
            if(job_list->pid_array != NULL) {
                for (int p = 0; p < job_list[i].ncommands; p++) {
                    printf(" %d ", job_list[i].pid_array[p]);
                }
            }
            printf("] | ");

            printf("Job ID: %d | Status: %s | Shown: %s | Command: %s", job_list[i].job_id, job_status, job_list[i].shown ? "true" : "false", job_list[i].command);
            printf("\n");
        }
        return;
    }

    // Pipes y procesos externos
    int **pipe_array = NULL;

    // Crear array de pipes si hay más de un comando
    if (cmd->ncommands > 1) {
        pipe_array = (int **)malloc((cmd->ncommands - 1) * sizeof(int *));
        for (int i = 0; i < cmd->ncommands - 1; i++) {
            pipe_array[i] = (int *)malloc(2 * sizeof(int));
            if (pipe(pipe_array[i]) < 0) {
                perror("Error creando pipe");
                exit(1);
            }
        }
    }

    pid_t pgid = 0; // ID del grupo de procesos para manejar el pipeline

    for (int j = 0; j < cmd->ncommands; j++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Error al crear proceso");
            exit(1);
        }

        if (pid == 0) { // Proceso hijo
            // Redirección de señales
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // Redirecciones para pipes
            if (j < cmd->ncommands - 1) { // No es el último, redirige salida
                dup2(pipe_array[j][1], STDOUT_FILENO);
            }
            if (j > 0) { // No es el primero, redirige entrada
                dup2(pipe_array[j - 1][0], STDIN_FILENO);
            }

            // Redirecciones de entrada/salida específicas
            if (j == 0 && cmd->redirect_input) {
                input_redirect(cmd->redirect_input);
            }
            if (j == cmd->ncommands - 1 && cmd->redirect_output) {
                output_redirect(cmd->redirect_output);
            }

            // Cerrar pipes no utilizados
            if (pipe_array != NULL) {
                for (int i = 0; i < cmd->ncommands - 1; i++) {
                    close(pipe_array[i][0]);
                    close(pipe_array[i][1]);
                }
            }

            // Ejecutar comando
            execvp(cmd->commands[j].argv[0], cmd->commands[j].argv);
            perror("Error al ejecutar comando");
            exit(1);
        } else { // Proceso padre
            if (j == 0) {
                // Configurar grupo de procesos para el pipeline
                pgid = pid;
                setpgid(pid, pgid);
            } else {
                setpgid(pid, pgid);
            }

            // Guardar el PID en fg_job si es foreground
            if (!cmd->background) {
                fg_job.pid_array[j] = pid;
            }

            // Cerrar pipes no utilizados
            if (pipe_array != NULL) {
                if (j > 0) { // Cerrar pipe de entrada
                    close(pipe_array[j - 1][0]);
                }
                if (j < cmd->ncommands - 1) { // Cerrar pipe de salida
                    close(pipe_array[j][1]);
                }
            }
        }
    }

    // Liberar memoria de pipes
    if (pipe_array != NULL) {
        for (int i = 0; i < cmd->ncommands - 1; i++) {
            free(pipe_array[i]);
        }
        free(pipe_array);
    }

    if (!cmd->background) { // Si es un proceso en foreground
        fg_job.status = RUNNING;

        // Esperar a que todos los procesos terminen o sean suspendidos
        int status;
        while ((waitpid(-pgid, &status, WUNTRACED)) > 0) {
            if (WIFSTOPPED(status)) { // Proceso suspendido
                fg_job.status = SUSPENDED;

                // Mover proceso a la lista de trabajos
                job_list[next_job].ncommands = fg_job.ncommands;
                strcpy(job_list[next_job].command, fg_job.command);
                job_list[next_job].status = SUSPENDED;
                job_list[next_job].shown = false;
                job_list[next_job].pid_array = (pid_t *)malloc(fg_job.ncommands * sizeof(pid_t));

                for (int i = 0; i < fg_job.ncommands; i++) {
                    job_list[next_job].pid_array[i] = fg_job.pid_array[i];
                }

                printf("\n[%d]+   Stopped                   %s\n", job_list[next_job].job_id, job_list[next_job].command);
                next_overwritable_job();
                break;
            } else if (WIFEXITED(status)) { // Proceso terminado
                fg_job.status = FINISHED;
            }
        }
    } else { // Si es background, mostrar el job ID y el último PID
        printf("[%d] %d\n", job_list[next_job].job_id, job_list[next_job].pid_array[cmd->ncommands - 1]);
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
    static char prev_dir[MAX_PATH] = ""; // Almacena el directorio anterior
    char current_dir[MAX_PATH];         // Buffer para el directorio actual

    const char *directory = NULL;

    // Si no se proporcionan argumentos, usar la variable HOME
    if (cmd->commands[0].argc == 1) {
        directory = getenv("HOME");
        if (directory == NULL) { // Manejar error si HOME no está definido
            fprintf(stderr, "Error: No se encontró la variable HOME\n");
            return;
        }
    } else if (strcmp(cmd->commands[0].argv[1], "-") == 0) {
        // Si el argumento es "-", cambiar al directorio anterior
        if (strlen(prev_dir) == 0) {
            fprintf(stderr, "Error: No hay un directorio anterior para cambiar\n");
            return;
        }
        directory = prev_dir;
    } else if (cmd->commands[0].argv[1][0] == '~') {
        // Expande el tilde (~) a HOME
        const char *home = getenv("HOME");
        if (home == NULL) {
            fprintf(stderr, "Error: No se encontró la variable HOME\n");
            return;
        }

        // Concatenar HOME con la ruta relativa después de '~'
        static char expanded_path[MAX_PATH];
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, cmd->commands[0].argv[1] + 1);
        directory = expanded_path;
    } else {
        // Usar el argumento proporcionado como ruta
        directory = cmd->commands[0].argv[1];
    }

    // Guardar el directorio actual antes de cambiar
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("Error al obtener el directorio actual");
        return;
    }

    // Intentar cambiar el directorio
    if (chdir(directory) != 0) {
        fprintf(stderr, "Error al cambiar a '%s': %s\n", directory, strerror(errno));
    } else {
        // Guardar el directorio anterior para el próximo "cd -"
        strncpy(prev_dir, current_dir, sizeof(prev_dir) - 1);
        prev_dir[sizeof(prev_dir) - 1] = '\0';

        // Mostrar el nuevo directorio actual
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            perror("Error al obtener el directorio actual después de cambiar");
        } else {
            printf("Directorio actual: %s\n", current_dir);
        }
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
    free(job_list);
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

// Cuando se detecta Ctrl+Z, cambia el estado del único job en foreground a "Suspendido" y muestra por pantalla su estado actual
void stop_handler(int signo) {
    if (fg_job.ncommands > 0 && fg_job.status == RUNNING) {
        // Enviar SIGTSTP al grupo de procesos del fg_job
        kill(-fg_job.pid_array[0], SIGTSTP); // -pid para enviar al grupo de procesos
    }
}
void check_jobs() {
    int i,j,k;
    int job_id_counter = 1;

    //Para mostrar el job_id en orden de colocación dentro de job_list, se recolocan los ids en función del contenido actual de job_list
    for(i=0; i<MAX_JOBS; i++){
        if(job_list[i].shown == false){
            job_list[i].job_id = job_id_counter;
            job_id_counter++;
        }
    }

    //Comprueba para cada uno de los jobs si han terminado, y actualiza su estado dentro de job_list
    for(i=0; i<MAX_JOBS; i++){
        if(job_list[i].command != NULL && job_list[i].pid_array != NULL) {
            int exit_status;
            pid_t pid = 0;
            pid_t current_job_pids[job_list[i].ncommands];

            for(j=0; j<job_list[i].ncommands; j++){

                if(job_list[i].status == RUNNING) {
                    pid = waitpid(job_list[i].pid_array[j], &exit_status, WNOHANG);
                }

                if(pid < 0){
                    perror("waitpid");
                    exit(1); //Sale de la minishell si hay algún error con los pids
                }

                current_job_pids[j] = pid;
            }

            bool all_finished = true;
            for(k=0; k<job_list[i].ncommands; k++){
                if(current_job_pids[k] == 0){
                    all_finished = false;
                    break;
                }
            }

            if(all_finished){
                if (WIFEXITED(exit_status)){
                    job_list[i].status = FINISHED; // Marcar como terminado
                    job_list[i].shown = false;    // Y marcar como todavía no mostrado
                }
                //Reinicia el hueco del job, porque nunca se muestra como abortado
                else if (WIFSIGNALED(exit_status)) {
                    free_job(&job_list[i]);
                }
            }
        }
    }
}

void next_overwritable_job() {
    for (int i = 0; i < MAX_JOBS; i++) {
        // Si el hueco está vacío o es un proceso en background mostrado, es válido
        if (job_list[i].status == FINISHED && job_list[i].ncommands == 0 && job_list[i].shown) {
            next_job = i;
            return; // Salir tras encontrar el primer hueco válido
        }
    }

    // Si no se encontró un hueco válido, manejar el error
    fprintf(stderr, "Error: No se encontró un hueco válido en job_list\n");
}

void fill_job(Tjob* job, tline* parsed_line) {
    //strcpy(job.command,""); //No debería de hacer falta, porque cuando se finaliza un proceso ya se establece el comando como ""
    for (int j = 0; j < parsed_line->ncommands; j++) {
        if (parsed_line->commands[j].argv != NULL) {
            for (int k = 0; k < parsed_line->commands[j].argc; k++) {
                strcat(job->command, parsed_line->commands[j].argv[k]);
                strcat(job->command, " ");
            }
        }
        if (j < parsed_line->ncommands - 1) {
            strcat(job->command,"| ");
        }
    }

    if(parsed_line->background) {
        strcat(job->command, "&");
    }
    job->ncommands = parsed_line->ncommands;
    job->status = RUNNING;
    job->shown = false;
    job->pid_array = (pid_t*)malloc(parsed_line->ncommands * sizeof(pid_t));
}

void free_job(Tjob* job) {
    job->status = FINISHED; // Marcar como abortado
    strcpy(job->command, "");
    job->ncommands = 0;
    if(job->pid_array != NULL) {
        free(job->pid_array);
        job->pid_array = NULL;
    }
    job->shown = true;
}

