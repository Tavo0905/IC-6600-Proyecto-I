#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>

#define MSGSZ 2048
#define MAX_PATH_SIZE 1024

struct msg_buffer {
    long msg_type;
    char msg_text[MSGSZ];
};

void copy_file(const char *src, const char *dst) {
    FILE *source = fopen(src, "r");
    if (source == NULL) {
        perror("Error opening source file");
        exit(EXIT_FAILURE);
    }

    FILE *dest = fopen(dst, "w");
    if (dest == NULL) {
        perror("Error opening destination file");
        fclose(source);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFSIZ];
    size_t bytes;

    while ((bytes = fread(buffer, 1, BUFSIZ, source)) > 0) {
        fwrite(buffer, 1, bytes, dest);
    }

    fclose(source);
    fclose(dest);
}

void worker(int msqid) {
    struct msg_buffer message;
    char src_path[MAX_PATH_SIZE], dst_path[MAX_PATH_SIZE];

    while (1) {
        if (msgrcv(msqid, &message, MSGSZ, 1, 0) == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }

        if (strcmp(message.msg_text, "DONE") == 0) {
            break;
        }

        sscanf(message.msg_text, "%s %s", src_path, dst_path);

        copy_file(src_path, dst_path);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source_directory> <destination_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *src_dir = argv[1];
    char *dst_dir = argv[2];

    DIR *dir;
    struct dirent *dp;
    struct msg_buffer message;
    int msqid;

    key_t key = ftok("msgq", 65);
    msqid = msgget(key, 0666 | IPC_CREAT);

    int pid;
    int num_workers = 4;  // Número de procesos en el pool

    for (int i = 0; i < num_workers; i++) {
        pid = fork();
        if (pid == 0) {
            worker(msqid);
            exit(EXIT_SUCCESS);
        }
    }

    if ((dir = opendir(src_dir)) == NULL) {
        perror("Error opening source directory");
        exit(EXIT_FAILURE);
    }

    char src_path[MAX_PATH_SIZE], dst_path[MAX_PATH_SIZE];

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, dp->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dp->d_name);

        snprintf(message.msg_text, sizeof(message.msg_text), "%s %s", src_path, dst_path);
        message.msg_type = 1;

        if (msgsnd(msqid, &message, strlen(message.msg_text) + 1, 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
    }

    closedir(dir);

    // Enviar mensajes de terminación a los trabajadores
    for (int i = 0; i < num_workers; i++) {
        snprintf(message.msg_text, sizeof(message.msg_text), "DONE");
        message.msg_type = 1;

        if (msgsnd(msqid, &message, strlen(message.msg_text) + 1, 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }
    }

    // Esperar a que todos los procesos hijos terminen
    for (int i = 0; i < num_workers; i++) {
        wait(NULL);
    }

    // Eliminar la cola de mensajes
    msgctl(msqid, IPC_RMID, NULL);

    printf("Directory copied successfully!\n");

    return 0;
}
