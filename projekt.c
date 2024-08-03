#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

//Zdefiniowanie nazwy pliku, do którego będzie się zapisywać historia komend
#define HISTORIA "historia.txt"
//Ile linii maksymalnie w historii
#define MAX_LINIE 20
//Maksymalna dlugosc polecenia
#define MAX_DLUGOSC_LINII 1024

//Deklaracja struktury do obslugi sygnalu
struct sigaction act;

//Flaga informujaca o przechwyceniu sygnalu
volatile sig_atomic_t przechwycono = 0;


//Funkcja do wykonywania pojedynczego polecenia
int wykonaj_komende(char *komenda, int output_fd, int nie_czekaj) {
    int status;
    pid_t pid = fork();

    char *argumenty[100];
    //Rozdzielanie polecenia na argumenty
    char *slowo = strtok(komenda, " ");
    int i = 0;

    while (slowo != NULL) {
        argumenty[i++] = slowo;
        slowo = strtok(NULL, " ");
    }
    argumenty[i] = NULL;

    //Jesli wyjscie jest inne niz stand. to przekieruj
    if (output_fd != STDOUT_FILENO) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }


    if (pid < 0) {
        perror("Błąd forkowania procesu");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        //Wykonywanie procesu potomnego
        if (execvp(argumenty[0], argumenty) == -1) {
            perror("Błąd execvp");
            exit(EXIT_FAILURE);
        }
    } else {
        //Czekanie na zakonczenie procesu potomnego
        if (!nie_czekaj) {
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
        }
    }
    return 0;
}

//Funkcja do obslugi polecenia, wykrycia potoku
void obsluz_polecenie(char *polecenia, const char *plik_wyjsciowy, int w_tle) {
    int liczba_potokow = 0;

    //Wykrywanie znaku potoku
    for (char *p = polecenia; *p; p++) {
        if (*p == '|') liczba_potokow++;
    }

    int potoki[liczba_potokow][2];
    for (int i = 0; i < liczba_potokow; i++) {
        if (pipe(potoki[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    int index_procesu = 0;
    char *polecenie = strtok(polecenia, "|");
    int output_fd = STDOUT_FILENO;

    //Otwarcie pliku do zapisu jesli wyjscie to nie STDOUT
    if (plik_wyjsciowy != NULL && strcmp(plik_wyjsciowy, "stdout") != 0) {
        output_fd = open(plik_wyjsciowy, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd == -1) {
            perror("Błąd otwarcia");
            exit(EXIT_FAILURE);
        }
    }

    while (polecenie) {
        if (fork() == 0) {
            if (index_procesu > 0) {
                dup2(potoki[index_procesu - 1][0], STDIN_FILENO);
            }
            if (index_procesu < liczba_potokow) {
                dup2(potoki[index_procesu][1], STDOUT_FILENO);
            }

            //Zamykanie deskryptorow plikow
            for (int i = 0; i < liczba_potokow; i++) {
                close(potoki[i][0]);
                close(potoki[i][1]);
            }

            if (index_procesu == liczba_potokow) {
                dup2(output_fd, STDOUT_FILENO);
            }

            //Wykonanie polecenia
            wykonaj_komende(polecenie, STDOUT_FILENO, w_tle);
            exit(EXIT_FAILURE);
        }

        //Wydziela kolejne polecenie po |
        polecenie = strtok(NULL, "|");
        index_procesu++;
    }

    for (int i = 0; i < liczba_potokow; i++) {
        close(potoki[i][0]);
        close(potoki[i][1]);
    }


    while (wait(NULL) > 0);

    //zamykanie pliku wyjsciowego
    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }
}

//Funkcja do odczytu historii
void odczytaj_historie(int sig) {

     przechwycono = 1;

    FILE *plik;
    char linia[MAX_DLUGOSC_LINII];

    //Otwarcie pliku w trybie odczytu
    plik = fopen(HISTORIA, "r");
    if (plik == NULL) {
        //Jesli plik nie istnieje to zostaje utworzony
        printf("Plik nie istnieje, zostal wlasnie stworzony\n");
        plik = fopen(HISTORIA, "w+");
        if (plik == NULL) {
            printf("Nie można utworzyć nowego pliku.\n");
            return;
        }
    } else {
        printf("Zawartość historii:\n");
        int i=1;
        while (fgets(linia, sizeof(linia), plik) != NULL ) {

            printf("%d) %s",i, linia);
            i++;


        }

    }
    //Zamykamy plik z historia
    fclose(plik);

}


int zapisz_historie(const char *linia) {
    //Pomijamy puste polecenia
    if (linia == NULL || linia[0] == '\0' || linia[0] == '\n') {
        return 1;
    }

    FILE *plik;
    char bufor[MAX_DLUGOSC_LINII];
    char *linie[MAX_LINIE];
    int licznik = 0;

    plik = fopen(HISTORIA, "r");
    if (plik != NULL) {

    //Skopiowanie i okreslenie ile wierszy jest w pliku
        while (fgets(bufor, sizeof(bufor), plik) != NULL && licznik < MAX_LINIE) {
            linie[licznik] = strdup(bufor);
            licznik++;
        }
        fclose(plik);
    }


    plik = fopen(HISTORIA, "w");
    if (plik == NULL) {
        //Jesli nie mozna otworzyc pliku to zwalniamy zajeta pamiec
        printf("Nie można otworzyć pliku HISTORIA.\n");
        for (int i = 0; i < licznik; i++) {
            free(linie[i]);

       }

        return -1;
    }

    // Jesli liczba wierszy jest za wysoka to usuwamy najstarsze
    if (licznik >= MAX_LINIE) {
        for (int i = 1; i < MAX_LINIE; i++) {
            free(linie[i - 1]);
            //Przesuwanie wierszy do góry
            linie[i - 1] = strdup(linie[i]);
        }
        //Zwalniamy pamiec by zapobiec wyciekowi pamieci
        free(linie[19]);

        licznik--;
    }


    linie[licznik++] = strdup(linia);

    //Zapisanie polecen do pliku i zwalnianie pamieci
    for (int i = 0; i < licznik; i++) {
        fputs(linie[i], plik);
        free(linie[i]);
    }

    fclose(plik);
    return 0;
}


int main() {
    //Struktura do obslugi sygnalow
    struct sigaction act;
    //Do reakcji na sygnal sluzy odczytaj_historie()
    act.sa_handler = odczytaj_historie;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    //Jesli otrzymamy SIGINT obsluz strukture act
    sigaction(SIGINT, &act, NULL);

    char zapisz[MAX_DLUGOSC_LINII];
    char polecenie[MAX_DLUGOSC_LINII];
    while (1) {

        if (przechwycono) {
            przechwycono = 0;
            continue;
        }

        printf("cmd> ");
        int w_tle = 0;
        if (fgets(polecenie, sizeof(polecenie), stdin) == NULL) {
            if (przechwycono) {
                przechwycono = 0;
                continue;
            }
            perror("Błąd fgets");
            return 1;
        }
        //Kopiujemy tresc polecenia do zapisz by zachowac >> & itp.
        memcpy(zapisz, polecenie, sizeof(polecenie));
        polecenie[strcspn(polecenie, "\n")] = 0;

        //Jesli polecenie to end to zakoncz dzialanie
        if (strcmp(polecenie, "end") == 0) {
            break;
        }

        char bufor[MAX_DLUGOSC_LINII];
        strcpy(bufor, polecenie);

        char *slowo;
        char *argumenty[64];
        int i = 0;
        slowo = strtok(bufor, " ");
        while (slowo != NULL) {
            argumenty[i++] = slowo;
            slowo = strtok(NULL, " ");
        }
        argumenty[i] = NULL;

        int czy_zapis = 0;
        char adres[MAX_DLUGOSC_LINII];
        char plik[MAX_DLUGOSC_LINII];
        plik[0] = '\0';

        // x sluzy do okreslenie ktory znak usunac w zaleznosci czy w ciagu jest & czy nie
        int x = 2;

        //Jesli ostatni znak to & to zmieniamy w_tle na true
        if (i > 1 && strcmp(argumenty[i - 1], "&") == 0) {
            w_tle = 1;
            x++;
        }

        //Jesli >> to 3 ostatni lub przedostatni znak to zmieniamy flage czy_zapis na 1
        //Nastepny wyraz po >> to nazwa pliku
        if (i > 2 && strcmp(argumenty[i - x], ">>") == 0) {
            czy_zapis = 1;
            strcpy(adres, argumenty[i - x + 1]);
        }

        //Usuwamy wykryte znaki kopiujac w ich miejsce pusty ciag
        char *ukryj = strstr(polecenie, "&");
        if (ukryj != NULL) {
            strcpy(ukryj, "");
        }

        char *przekieruj = strstr(polecenie, ">>");
        if (przekieruj != NULL) {
            strcpy(przekieruj, "");
        }

        //Jesli chcemy zapisac wynik w pliku to zostanie on zapisany w pliku adres
        if (czy_zapis == 1) {
            obsluz_polecenie(polecenie, adres, w_tle);
        } else {
            //Jesli nie to wynik zostanie wyswietlony na stand. wyjsciu
            obsluz_polecenie(polecenie, "stdout", w_tle);
        }

   //Zapisujemy polecenie
   zapisz_historie(zapisz);
    }

    return 0;
}
