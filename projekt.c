#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>

//Zdefiniowanie nazwy pliku, do którego będzie się zapisywać historia komend
#define HISTORY_FILE "history.txt"
//Ile linii maksymalnie w historii
#define MAX_HISTORY_LINES 20
//Maksymalna dlugosc polecenia
#define MAX_LINE_LENGTH 1024

//Deklaracja struktury do obslugi sygnalu
struct sigaction act;

//Flaga informujaca o przechwyceniu sygnalu
volatile sig_atomic_t signal_caught = 0;


//Funkcja do wykonywania pojedynczego polecenia
int execute_command(char *command_line, int output_fd, int no_wait) {
    int exit_status;
    pid_t child_pid = fork();

    char *args[100];
    //Rozdzielanie polecenia na argumenty
    char *token = strtok(command_line, " ");
    int arg_count = 0;

    while (token != NULL) {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    //Jesli wyjscie jest inne niz stand. to przekieruj
    if (output_fd != STDOUT_FILENO) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }


    if (child_pid < 0) {
        perror("Fork error");
        exit(EXIT_FAILURE);
    } else if (child_pid == 0) {
        //Wykonywanie procesu potomnego
        if (execvp(args[0], args) == -1) {
            perror("execvp error");
            exit(EXIT_FAILURE);
        }
    } else {
        //Czekanie na zakonczenie procesu potomnego
        if (!no_wait) {
            waitpid(child_pid, &exit_status, 0);
            if (WIFEXITED(exit_status)) {
                return WEXITSTATUS(exit_status);
            }
        }
    }
    return 0;
}

//Funkcja do obslugi polecenia, wykrycia potoku
void handle_command(char *pipeline, const char *output_file, int in_background) {
    int pipe_count = 0;

    //Wykrywanie znaku potoku
    for (char *ch = pipeline; *ch; ch++) {
        if (*ch == '|') pipe_count++;
    }

    int pipes[pipe_count][2];
    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    int stage_index = 0;
    char *stage_command = strtok(pipeline, "|");
    int output_fd = STDOUT_FILENO;

    //Otwarcie pliku do zapisu jesli wyjscie to nie STDOUT
    if (output_file != NULL && strcmp(output_file, "stdout") != 0) {
        output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd == -1) {
            perror("Open error");
            exit(EXIT_FAILURE);
        }
    }

    while (stage_command) {
        if (fork() == 0) {
            if (stage_index > 0) {
                dup2(pipes[stage_index - 1][0], STDIN_FILENO);
            }
            if (stage_index < pipe_count) {
                dup2(pipes[stage_index][1], STDOUT_FILENO);
            }

            //Zamykanie deskryptorow plikow
            for (int i = 0; i < pipe_count; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }

            if (stage_index == pipe_count) {
                dup2(output_fd, STDOUT_FILENO);
            }

            //Wykonanie polecenia
            execute_command(stage_command, STDOUT_FILENO, in_background);
            exit(EXIT_FAILURE);
        }

        //Wydziela kolejne polecenie po |
        stage_command = strtok(NULL, "|");
        stage_index++;
    }

    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }


    while (wait(NULL) > 0);

    //zamykanie pliku wyjsciowego
    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }
}

//Funkcja do odczytu historii
void print_history(int signal_number) {

    signal_caught = 1;

    FILE *history_file;
    char line[MAX_LINE_LENGTH];

    //Otwarcie pliku w trybie odczytu
    history_file = fopen(HISTORY_FILE, "r");
    if (history_file == NULL) {
        //Jesli plik nie istnieje to zostaje utworzony
        printf("File does not exist, it has just been created\n");
        history_file = fopen(HISTORY_FILE, "w+");
        if (history_file == NULL) {
            printf("Could not create a new file.\n");
            return;
        }
    } else {
        printf("History contents:\n");
        int line_number = 1;
        while (fgets(line, sizeof(line), history_file) != NULL) {

            printf("%d) %s", line_number, line);
            line_number++;


        }

    }
    //Zamykamy plik z historia
    fclose(history_file);

}


int save_history(const char *new_entry) {
    //Pomijamy puste polecenia
    if (new_entry == NULL || new_entry[0] == '\0' || new_entry[0] == '\n') {
        return 1;
    }

    FILE *history_file;
    char read_buffer[MAX_LINE_LENGTH];
    char *history_lines[MAX_HISTORY_LINES];
    int line_count = 0;

    history_file = fopen(HISTORY_FILE, "r");
    if (history_file != NULL) {

        //Skopiowanie i okreslenie ile wierszy jest w pliku
        while (fgets(read_buffer, sizeof(read_buffer), history_file) != NULL && line_count < MAX_HISTORY_LINES) {
            history_lines[line_count] = strdup(read_buffer);
            line_count++;
        }
        fclose(history_file);
    }


    history_file = fopen(HISTORY_FILE, "w");
    if (history_file == NULL) {
        //Jesli nie mozna otworzyc pliku to zwalniamy zajeta pamiec
        printf("Could not open the history file.\n");
        for (int i = 0; i < line_count; i++) {
            free(history_lines[i]);

        }

        return -1;
    }

    // Jesli liczba wierszy jest za wysoka to usuwamy najstarsze
    if (line_count >= MAX_HISTORY_LINES) {
        for (int i = 1; i < MAX_HISTORY_LINES; i++) {
            free(history_lines[i - 1]);
            //Przesuwanie wierszy do góry
            history_lines[i - 1] = strdup(history_lines[i]);
        }
        //Zwalniamy pamiec by zapobiec wyciekowi pamieci
        free(history_lines[MAX_HISTORY_LINES - 1]);

        line_count--;
    }


    history_lines[line_count++] = strdup(new_entry);

    //Zapisanie polecen do pliku i zwalnianie pamieci
    for (int i = 0; i < line_count; i++) {
        fputs(history_lines[i], history_file);
        free(history_lines[i]);
    }

    fclose(history_file);
    return 0;
}


int main() {
    //Struktura do obslugi sygnalow
    struct sigaction act;
    //Do reakcji na sygnal sluzy print_history()
    act.sa_handler = print_history;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    //Jesli otrzymamy SIGINT obsluz strukture act
    sigaction(SIGINT, &act, NULL);

    char raw_input[MAX_LINE_LENGTH];
    char command_line[MAX_LINE_LENGTH];
    while (1) {

        if (signal_caught) {
            signal_caught = 0;
            continue;
        }

        printf("cmd> ");
        int in_background = 0;
        if (fgets(command_line, sizeof(command_line), stdin) == NULL) {
            if (signal_caught) {
                signal_caught = 0;
                continue;
            }
            perror("fgets error");
            return 1;
        }
        //Kopiujemy tresc polecenia do raw_input by zachowac >> & itp.
        memcpy(raw_input, command_line, sizeof(command_line));
        command_line[strcspn(command_line, "\n")] = 0;

        //Jesli polecenie to end to zakoncz dzialanie
        if (strcmp(command_line, "end") == 0) {
            break;
        }

        char parse_buffer[MAX_LINE_LENGTH];
        strcpy(parse_buffer, command_line);

        char *token;
        char *words[64];
        int word_count = 0;
        token = strtok(parse_buffer, " ");
        while (token != NULL) {
            words[word_count++] = token;
            token = strtok(NULL, " ");
        }
        words[word_count] = NULL;

        int redirect_to_file = 0;
        char output_path[MAX_LINE_LENGTH];

        // redirect_offset okresla ktory od konca wyraz to >> w zaleznosci czy w ciagu jest & czy nie
        int redirect_offset = 2;

        //Jesli ostatni znak to & to zmieniamy in_background na true
        if (word_count > 1 && strcmp(words[word_count - 1], "&") == 0) {
            in_background = 1;
            redirect_offset++;
        }

        //Jesli >> to 3 ostatni lub przedostatni znak to zmieniamy flage redirect_to_file na 1
        //Nastepny wyraz po >> to nazwa pliku
        if (word_count > 2 && strcmp(words[word_count - redirect_offset], ">>") == 0) {
            redirect_to_file = 1;
            strcpy(output_path, words[word_count - redirect_offset + 1]);
        }

        //Usuwamy wykryte znaki kopiujac w ich miejsce pusty ciag
        char *ampersand_pos = strstr(command_line, "&");
        if (ampersand_pos != NULL) {
            strcpy(ampersand_pos, "");
        }

        char *redirect_pos = strstr(command_line, ">>");
        if (redirect_pos != NULL) {
            strcpy(redirect_pos, "");
        }

        //Jesli chcemy zapisac wynik w pliku to zostanie on zapisany w pliku output_path
        if (redirect_to_file == 1) {
            handle_command(command_line, output_path, in_background);
        } else {
            //Jesli nie to wynik zostanie wyswietlony na stand. wyjsciu
            handle_command(command_line, "stdout", in_background);
        }

        //Zapisujemy polecenie
        save_history(raw_input);
    }

    return 0;
}
