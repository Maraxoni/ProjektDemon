#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <openssl/evp.h>

#define MAX_PATH_LENGTH 4096

// Funkcja do wyliczania sumy kontrolnej pliku przy użyciu SHA-256
void computeSHA(unsigned char* shaHash, const char* filePath) {
    int file = open(filePath, O_RDONLY);
    if (file == -1) {
        // Obsługa błędu otwarcia pliku
        return;
    }

    EVP_MD_CTX* shaContext = EVP_MD_CTX_new();
    const EVP_MD* shaAlgorithm = EVP_sha256();
    EVP_DigestInit_ex(shaContext, shaAlgorithm, NULL);

    unsigned char buffer[MAX_PATH_LENGTH];
    ssize_t bytesRead;

    while ((bytesRead = read(file, buffer, sizeof(buffer))) > 0) {
        EVP_DigestUpdate(shaContext, buffer, bytesRead);
    }

    close(file);

    EVP_DigestFinal_ex(shaContext, shaHash, NULL);
    EVP_MD_CTX_free(shaContext);
}

long getFileSize(const char* filePath) {
    int fd = open(filePath, O_RDONLY);
    if (fd == -1) {
        perror("Błąd otwierania pliku");
        return -1;
    }

    // Przesuwamy wskaźnik pliku na koniec
    off_t offset = lseek(fd, 0, SEEK_END);
    if (offset == -1) {
        perror("Błąd ustawienia wskaźnika");
        close(fd);
        return -1;
    }

    // Pobieramy aktualną pozycję wskaźnika, co jest równoznaczne z rozmiarem pliku
    long size = (long)offset;

    close(fd);
    return size;
}

void copyFile(const char* srcPath, const char* dstPath) {
    int srcFile, dstFile;
    char buffer[4096];
    ssize_t bytesRead, bytesWritten;

    // Otwórz plik źródłowy w trybie do odczytu
    srcFile = open(srcPath, O_RDONLY);
    if (srcFile == -1) {
        perror("Błąd przy otwieraniu pliku źródłowego");
        return;
    }

    // Otwórz plik docelowy w trybie do zapisu, utwórz go jeśli nie istnieje
    dstFile = open(dstPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dstFile == -1) {
        perror("Błąd przy otwieraniu pliku docelowego");
        close(srcFile);
        return;
    }

    // Kopiuj zawartość pliku ze źródła do celu
    while ((bytesRead = read(srcFile, buffer, sizeof(buffer))) > 0) {
        bytesWritten = write(dstFile, buffer, bytesRead);
        if (bytesWritten == -1) {
            perror("Błąd przy zapisie do pliku docelowego");
            break;
        }
    }

    // Sprawdź, czy wystąpił błąd podczas odczytu
    if (bytesRead == -1) {
        perror("Błąd przy odczycie pliku źródłowego");
    }

    // Zamknij pliki
    close(srcFile);
    close(dstFile);
}


void copyFileBig(const char* srcPath, const char* dstPath) {
    int srcFile, dstFile;
    off_t offset = 0;

    // Otwórz plik źródłowy w trybie do odczytu
    srcFile = open(srcPath, O_RDONLY);
    if (srcFile == -1) {
        perror("Błąd przy otwieraniu pliku źródłowego");
        return;
    }

    // Otwórz plik docelowy w trybie do zapisu, utwórz go jeśli nie istnieje
    dstFile = open(dstPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dstFile == -1) {
        perror("Błąd przy otwieraniu pliku docelowego");
        close(srcFile);
        return;
    }

    // Skopiuj zawartość pliku ze źródła do celu
    ssize_t bytesSent;
    while ((bytesSent = sendfile(dstFile, srcFile, &offset, 4096)) > 0) {
        // Sprawdź, czy wystąpił błąd podczas kopiowania
        if (bytesSent == -1) {
            perror("Błąd przy kopiowaniu pliku");
            break;
        }
    }

    // Zamknij pliki
    close(srcFile);
    close(dstFile);
}


void deleteFile(const char* filePath) {
    printf("no: %s\n",filePath);
    if (unlink(filePath) == 0) {
        printf("Plik %s zostal usuniety.\n", filePath);
    }
    else {
        perror("Blad usuwania pliku");
    }
}



void deleteDirectory(const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if (dir == NULL) {
        perror("Błąd otwierania katalogu");
        exit(EXIT_FAILURE);
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

            if (entry->d_type == DT_DIR) {
                deleteDirectory(path);
            } else {
                deleteFile(path);
            }
        }
    }

    closedir(dir);

    if (rmdir(dir_path) != 0) {
        perror("Błąd przy usuwaniu katalogu");
        exit(EXIT_FAILURE);
    }
}

void monitorDelete(const char* dir1Path, const char* dir2Path, bool rek) {
    DIR* dir1 = opendir(dir1Path);
    DIR* dir2 = opendir(dir2Path);

    // Sprawdzenie, czy istnieje katalog 1
    if (dir1 == NULL) {
        perror("Błąd otwierania katalogu 1");
        exit(EXIT_FAILURE);
    }
    // Sprawdzenie, czy istnieje katalog 2
    if (dir2 == NULL) {
        perror("Błąd otwierania katalogu 2");
        exit(EXIT_FAILURE);
    }

    char srcFilePath[512], dstFilePath[512];
    unsigned char hash1[MAX_PATH_LENGTH];
    unsigned char hash2[MAX_PATH_LENGTH];

    // Przeiterowanie przez pliki w katalogu 2
    struct dirent* dir2Entry;
    while ((dir2Entry = readdir(dir2)) != NULL) {
        if (dir2Entry->d_type == DT_REG && strcmp(dir2Entry->d_name, ".") != 0 && strcmp(dir2Entry->d_name, "..") != 0) {
            int found = 0;
            rewinddir(dir1);

            struct dirent* dir1Entry;
            while ((dir1Entry = readdir(dir1)) != NULL) {
                if (dir1Entry->d_type == DT_REG && strcmp(dir1Entry->d_name, dir2Entry->d_name) == 0) {
                    found = 1;
                    break;
                }
            }

            if (found == 0) {
                snprintf(dstFilePath, sizeof(dstFilePath), "%s/%s", dir2Path, dir2Entry->d_name);
                deleteFile(dstFilePath);
            }
        }
    }

    rewinddir(dir2);

    // Przeiterowanie przez katalogi w katalogu 2
    while ((dir2Entry = readdir(dir2)) != NULL) {
        if (dir2Entry->d_type == DT_DIR && rek == true && strcmp(dir2Entry->d_name, ".") != 0 &&strcmp(dir2Entry->d_name, "..") != 0) {
    int found = 0;
    rewinddir(dir1);   
    struct dirent* dir1Entry;
        while ((dir1Entry = readdir(dir1)) != NULL) {
            if (dir1Entry->d_type == DT_DIR && strcmp(dir1Entry->d_name, dir2Entry->d_name) == 0) {
                found = 1;
                break;
            }
        }

        if (found == 0) {
            snprintf(dstFilePath, sizeof(dstFilePath), "%s/%s", dir2Path, dir2Entry->d_name);
            char sub_dir_path[512];
            snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/%s", dir2Path, dir2Entry->d_name);
            deleteDirectory(sub_dir_path);
        } else {
            char sub_dir1Path[512], sub_dir2Path[512];
            snprintf(sub_dir1Path, sizeof(sub_dir1Path), "%s/%s", dir1Path, dir2Entry->d_name);
            snprintf(sub_dir2Path, sizeof(sub_dir2Path), "%s/%s", dir2Path, dir2Entry->d_name);
            monitorDelete(sub_dir1Path, sub_dir2Path, rek);
        }
    }
}

closedir(dir1);
closedir(dir2);

}

void monitorCatalogue(const char* dir1Path, const char* dir2Path, bool rek, long size) {
    // Otworzenie katalogow
    DIR* dir1 = opendir(dir1Path);
	DIR* dir2 = opendir(dir2Path);

    // Sprawdzenie, czy istnieje katalog 1
    if (dir1 == NULL) {
        perror("Błąd otwierania katalogu 1");
		exit(EXIT_FAILURE);
    }
    // Sprawdzenie, czy istnieje katalog 2
    if (dir2 == NULL) {
        perror("Błąd otwierania katalogu 2");
		exit(EXIT_FAILURE);
    }
    // Deklaracja sciezek
    char srcFilePath[512], dstFilePath[512];
    unsigned char hash1[MAX_PATH_LENGTH];
    unsigned char hash2[MAX_PATH_LENGTH];
       
    // Przeiterowanie przez pliki w katalogu 1
    struct dirent* dir1Entry;
    while ((dir1Entry = readdir(dir1)) != NULL) {
        if (dir1Entry->d_type == DT_REG && strcmp(dir1Entry->d_name, ".") != 0 && strcmp(dir1Entry->d_name, "..") != 0) {
            // Przeiterowanie przez pliki w katalogu 2, w celu znalezienia brakujacych plikow w katalogu docelowym
            int found = 0;
            rewinddir(dir2);
            struct dirent* dir2Entry;
            while ((dir2Entry = readdir(dir2)) != NULL) {
                snprintf(srcFilePath, sizeof(srcFilePath), "%s/%s", dir1Path, dir1Entry->d_name);
                snprintf(dstFilePath, sizeof(dstFilePath), "%s/%s", dir2Path, dir1Entry->d_name);
                computeSHA(hash1,srcFilePath);
                computeSHA(hash2,dstFilePath);
                if (dir2Entry->d_type == DT_REG && strcmp(hash1, hash2) == 0) {
                    found = 1;
                    break;
                }
            }
            // Jezeli nie znaleziono zadnych pasujacych plikow to wykonywane jest kopiowanie
            if (found == 0) {
                if(getFileSize(srcFilePath)<size)
                {
                    copyFile(srcFilePath, dstFilePath);
                }
                else
                {
                    copyFileBig(srcFilePath, dstFilePath);
                }
            }
        }

        else if (dir1Entry->d_type == DT_DIR && rek == true && strcmp(dir1Entry->d_name, ".") != 0 && strcmp(dir1Entry->d_name, "..") != 0) {
            int found = 0;
            rewinddir(dir2);

            struct dirent* dir2Entry;
            while ((dir2Entry = readdir(dir2)) != NULL) {
                if (dir2Entry->d_type == DT_DIR && strcmp(dir1Entry->d_name, dir2Entry->d_name) == 0) {
                    found = 1;
                    break;
                }
            }

            
                char srcCatPath[512], dstCatPath[512];
                snprintf(srcCatPath, sizeof(srcCatPath), "%s/%s", dir1Path, dir1Entry->d_name);
                snprintf(dstCatPath, sizeof(dstCatPath), "%s/%s", dir2Path, dir1Entry->d_name);

                if (found == 0) {
                    if (mkdir(dstCatPath, 0777) == -1) {
                        perror("Błąd przy tworzeniu katalogu");
                        exit(EXIT_FAILURE);
                    }
                }

                monitorCatalogue(srcCatPath, dstCatPath, rek, size);
            
        }
            
            char sub_dir1Path[512], sub_dir2Path[512];
            snprintf(sub_dir1Path, sizeof(sub_dir1Path), "%s/%s", dir1Path, dir1Entry->d_name);
            snprintf(sub_dir2Path, sizeof(sub_dir2Path), "%s/%s", dir2Path, dir1Entry->d_name);
            
    }
    
    closedir(dir1);
    closedir(dir2);

}

int main(int argc, char* argv[]) {
	//argv[0] = sciezka zrodlowa
	//argv[1] = sciezka docelowa

	if (argc < 2 && argc > 5) {
		fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

    if (strcmp(argv[1],"." ) == 0 || strcmp(argv[1],"./" ) == 0 ) {
		fprintf(stderr, "Can't monitor current catalogue [ %s ], your memory will have a bad time!\n", argv[1]);
		exit(EXIT_FAILURE);
	}
    //Zmiana argumentow na sciezki
	printf("Katalog zrodlowy: %s\n",argv[0]);
	printf("Katalog docelowy: %s\n",argv[1]);
	char path1[PATH_MAX];
	char path2[PATH_MAX];
	snprintf(path1, sizeof(path1),"%s" , argv[0]);
	snprintf(path2, sizeof(path2),"%s" , argv[1]);
	strcat(path1,"Data");
    printf("Nowa wartosc zmiennej path1: %s\n", path1);
    printf("Nowa wartosc zmiennej path2: %s\n", path2);
	sleep(1);
    //Deklaracja zmiennych
    bool rek = false;
    long size = 10;
    int time = 300, choice = 0;
    
    //Wczytanie argumentow przez getopta
	while((choice = getopt(argc,argv,":t:s:R"))!= -1)
	{
		switch(choice)
		{
			case 'R':
                rek = true;
				break;
			case 't':
                time = atoi(optarg);
				break;
			case 's':
                size = atoi(optarg);
				break;
			default:
				break;
		}
	}
    size = size * 1024 * 1024;
    //Kontrola
    printf("Nowa wartosc zmiennej time: %d\n", time);
    printf("Nowa wartosc zmiennej size: %d\n", size);

    //Forkowanie
	pid_t pid, sid;
	pid = fork();

	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	sid = setsid();
	if (sid < 0) {

		exit(EXIT_FAILURE);
	}

	/* Petla Demona */
	while (1) {
		pid = fork();
		if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}

		if (pid == 0)
		{
			monitorCatalogue(path2,path1,rek,size);
            monitorDelete(path2,path1,rek);
			exit(EXIT_SUCCESS);
		}
		sleep(time);
	}
	exit(EXIT_SUCCESS);
}
