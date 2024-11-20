# <p align="center"> Minishell </p>
***   
## ⌨️ General Information

### 🖋️ Idea
This minishell runs several Linux commands using a parent-child process implementation: the main process (that holds the shell) creates a fork and execute the required command in a child subprocess. This implementation makes possible to control and modify each command´s execution flow directly from the parent process.


### 👷 Team members
| Name | URJC mail | Github user |
| ------------- | ------------- | ----------- |
| Lucía Domínguez Rodrigo | l.dominguez.2021@alumnos.urjc.es | [@LuciaDominguezRodrigo](https://github.com/LuciaDominguezRodrigo) |
| Marcos Jiménez Pulido  | m.jimenezp.2021@alumnos.urjc.es | [@MarJ03](https://github.com/MarJ03) |


## 🚀 Main functions

### ✔️ input and output redirections
Standard input (stdin) of the first command and standard output (stdout) of the last command can be modified. 
Also, it is possible to redirect standard error output of the last command.

### ✔️ foreground and background command execution
Recognizes and executes single commands, or two or more commands connected to a pipe, with 0 or more arguments in both foreground ("fg" command) and background ("bg" command) modes.

### ✔️ Complete "cd" command
Changes the current working directory.

### ✔️ Complete "jobs" command
Show an enumerated list indicating the process states in the current session.

### ✔️ Complete "exit" command
Ends the current section in the shell.

### ✔️ Complete "umask" command
Modify file creation mask according with an octal number provided as first argument. Indicates the permissions that will not be given when creating new files.


## 📥 Steps for executing the program
 - Open the project in an IDE (like CLion or Visual Studio Code). Is important to execute this project with Linux, regardless the extension (could be Ubuntu, Debian, KaliLinux, etc.).
 - Open a terminal (indicated in the options that the IDE ofers)

 🔬 Executing parser test program
 - Execute compila.sh inside parser folder, writing ./compila.sh after the prompt.
 - After that, for executing fuctions, write after the prompt: ./parser texttoparse.
 - For executing fuctions with files, write after the prompt: ./parser texttoparse < .fileRoute/filename.txt.

 💻 Executing minishell
 - Execute compila.sh file from the project root, writing ./compila.sh after the prompt.
 - After that, for executing fuctions, write after the prompt: ./minishell texttoexecute.
 - For executing fuctions with files, write after the prompt: ./minishell texttoexecute < .fileRoute/filename.txt.
