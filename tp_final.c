#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#define MEMORY_THRESHOLD (1024 * 1024)  // 1 Mo (en kilo-octets)
#define CPU_THRESHOLD 2.0               // Seuil d'utilisation du CPU (en pourcentage)
#define LOG_FILE "log.txt"
#define STOP_FILE "stop"
#define MAX_POINTS 50     
#define EMAIL "christlearn834@gmail.com"

float cpu_history[MAX_POINTS] = {0}; 
volatile sig_atomic_t stop_monitoring = 0;

// Fonction pour afficher "sysCPU" en grand et en vert
void print_graffiti_text() {
    printf("\033[1;32m");  // Activer la couleur verte
    printf("                                /$$$$$$  /$$$$$$$  /$$   /$$\n");
    printf("                               /$$__  $$| $$__  $$| $$  | $$\n");
    printf("  /$$$$$$$ /$$   /$$  /$$$$$$$| $$  \\__/| $$  \\ $$| $$  | $$\n");
    printf(" /$$_____/| $$  | $$ /$$_____/| $$      | $$$$$$$/| $$  | $$\n");
    printf("|  $$$$$$ | $$  | $$|  $$$$$$ | $$      | $$____/ | $$  | $$\n");
    printf(" \\____  $$| $$  | $$ \\____  $$| $$    $$| $$      | $$  | $$\n");
    printf(" /$$$$$$$/|  $$$$$$$ /$$$$$$$/|  $$$$$$/| $$      |  $$$$$$/\n");
    printf("|_______/  \\____  $$|_______/  \\______/ |__/       \\______/\n");
    printf("           /$$  | $$                                        \n");
    printf("          |  $$$$$$/                                        \n");
    printf("           \\______/                                         \n");
    printf("\033[0m\n");  // Réinitialiser la couleur
}


void handle_sigint(int sig) {
    (void)sig;
    stop_monitoring = 1;
}

void beep() {
    printf("\a");  // Alerte sonore
}

void speak(const char* text) {
    if (system("which espeak > /dev/null 2>&1") != 0) {
        printf("espeak n'est pas installé. Impossible de lire le message.\n");
        return;
    }

    char command[256];
    snprintf(command, sizeof(command), "espeak -v fr \"%s\"", text);
    system(command);
}

void send_email_alert(float cpu_usage, char *name, char *email) {
    if (email == NULL || strlen(email) == 0) {
        printf("Erreur: adresse email manquante.\n");
        return;
    }

    // Prepare the command string
    char command[512];
    snprintf(command, sizeof(command),
             "echo 'Bonjour %s, Alerte ! Utilisation CPU à %.2f%% dépasse le seuil.' | mail -s 'Alerte CPU élevée' %s", name, cpu_usage, email);

    int result = system(command);

    if (result != 0) {
        printf("Erreur lors de l'envoi de l'alerte à %s\n", email);
    }
}

float get_cpu_usage() {
    FILE *stat;
    unsigned long user1, nice1, system1, idle1;
    unsigned long user2, nice2, system2, idle2;

    // Première lecture
    stat = fopen("/proc/stat", "r");
    if (!stat) {
        perror("Erreur lors de l'ouverture de /proc/stat");
        return -1.0;
    }
    fscanf(stat, "cpu %lu %lu %lu %lu", &user1, &nice1, &system1, &idle1);
    fclose(stat);

    sleep(1); // Attendre 1 seconde pour un deuxième échantillon

    // Deuxième lecture
    stat = fopen("/proc/stat", "r");
    if (!stat) {
        perror("Erreur lors de l'ouverture de /proc/stat");
        return -1.0;
    }
    fscanf(stat, "cpu %lu %lu %lu %lu", &user2, &nice2, &system2, &idle2);
    fclose(stat);

    unsigned long total1 = user1 + nice1 + system1 + idle1;
    unsigned long total2 = user2 + nice2 + system2 + idle2;
    unsigned long total_delta = total2 - total1;
    unsigned long usage_delta = (user2 + nice2 + system2) - (user1 + nice1 + system1);

    return (total_delta == 0) ? 0.0 : ((float)usage_delta / total_delta) * 100.0;
}

void display_cpu_graph() {
    const int graph_height = 10;  // Hauteur de la courbe
    static int first_display = 1;  // Indicateur pour la première affichage

    if (first_display) {
        printf("\n\033[1;34m[CPU Usage Graph]\033[0m\n");

        // Dessiner la courbe
        for (int level = graph_height; level >= 0; --level) {
            printf("%2d%% | ", level * 10);

            for (int i = 0; i < MAX_POINTS; i++) {
                float value = cpu_history[i];

                if (value >= level * 10 && value < (level + 1) * 10) {
                    if (value >= CPU_THRESHOLD) {
                        printf("\033[1;31mO\033[0m");  // Point rouge (CPU dépasse seuil)
                    } else {
                        printf("*");
                    }
                } else {
                    printf(" ");
                }
            }
            printf("\n");
        }

        printf("    +-------------------------------------------------->\n");
        printf("       0s                          Temps (s)\n\n");

        first_display = 0;  // Marquer que la courbe a été affichée
    } else {
        // Mettre à jour les données sans réafficher la courbe
        for (int i = 0; i < MAX_POINTS - 1; i++) {
            cpu_history[i] = cpu_history[i + 1];
        }
        cpu_history[MAX_POINTS - 1] = get_cpu_usage();  // Fonction pour obtenir la nouvelle valeur CPU
    }
}

void log_system_info(char *name, char *email) {
    FILE *meminfo = fopen("/proc/meminfo", "r");
    FILE *log = fopen(LOG_FILE, "a");

    if (!meminfo || !log) {
        perror("Erreur lors de l'ouverture des fichiers");
        if (meminfo) fclose(meminfo);
        if (log) fclose(log);
        return;
    }

    char line[256];
    unsigned long total_mem = 0, free_mem = 0;

    while (fgets(line, sizeof(line), meminfo)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %lu kB", &total_mem);
        }
        if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line, "MemFree: %lu kB", &free_mem);
        }
    }
    fclose(meminfo);

    float cpu_usage = get_cpu_usage();

    time_t now = time(NULL);
    fprintf(log, "Timestamp: %s", ctime(&now));
    fprintf(log, "Memory Usage: Total = %lu kB, Free = %lu kB\n", total_mem, free_mem);
    fprintf(log, "CPU Usage: %.2f%%\n\n", cpu_usage);
    fclose(log);

    if (free_mem < MEMORY_THRESHOLD) {
        beep();
    }

    if (cpu_usage > CPU_THRESHOLD) {
        char message[256];
        snprintf(message, sizeof(message), "Alerte! L'utilisation du CPU est de %.2f%%.", cpu_usage);
        speak(message);
        send_email_alert(cpu_usage, name, email);
        display_cpu_graph();

    }

    // Update the CPU history
    for (int i = 1; i < MAX_POINTS; i++) {
        cpu_history[i - 1] = cpu_history[i];
    }
    cpu_history[MAX_POINTS - 1] = cpu_usage;

}

void monitor_system(char *name, char *email) {
    while (!stop_monitoring) {
        log_system_info(name, email);

        if (access(STOP_FILE, F_OK) == 0) {
            printf("Fichier 'stop' détecté. Arrêt de la surveillance.\n");
            break;
        }

        sleep(10);
    }
}


void doc(){
    printf(
        "SYNOPSIS\n"
        "  syscpu <Nom> <Email>\n\n"
        
        "DESCRIPTION\n"
        "  syscpu est un programme de surveillance des ressources système qui mesure l'utilisation "
        "du CPU et de la mémoire d'un système Linux. Lorsqu'un seuil critique est dépassé (pour "
        "l'utilisation du CPU ou de la mémoire), il envoie une alerte par email, génère une alerte "
        "sonore et affiche un graphique textuel de l'utilisation du CPU en temps réel.\n\n"

        "ARGUMENTS\n"
        "  <Nom>\n"
        "    Le nom de l'utilisateur qui sera affiché dans les messages et alertes.\n\n"
        
        "  <Email>\n"
        "    L'adresse email où les alertes seront envoyées en cas de dépassement des seuils d'utilisation du CPU ou de la mémoire.\n\n"

        "OPTIONS\n"
        "  Aucune option supplémentaire n'est disponible. Le programme prend uniquement deux arguments positionnels : le nom et l'email.\n\n"
        
        "FONCTIONNALITÉS\n"
        "  1. Surveillance du CPU\n"
        "     syscpu mesure l'utilisation du CPU en calculant le pourcentage d'utilisation à partir des informations du fichier /proc/stat. "
        "     Si l'utilisation du CPU dépasse 2, une alerte par email est envoyée.\n\n"
        
        "  2. Surveillance de la mémoire\n"
        "     Le programme lit les informations de mémoire disponibles dans /proc/meminfo. Si la mémoire libre tombe en dessous de 1 Mo, une alerte sonore est émise.\n\n"
        
        "  3. Alerte par email\n"
        "     Lorsqu'un seuil d'utilisation du CPU ou de la mémoire est dépassé, une alerte par email est envoyée à l'adresse spécifiée. "
        "     L'email contient des informations détaillées sur l'utilisation des ressources.\n\n"
        
        "  4. Graphique de l'utilisation du CPU\n"
        "     Un graphique textuel est affiché dans le terminal, montrant l'évolution de l'utilisation du CPU. Des points rouges (O) sur le graphique "
        "     indiquent que l'utilisation du CPU dépasse le seuil.\n\n"
        
        "  5. Alertes sonores et vocales\n"
        "     En cas de dépassement des seuils, une alerte sonore est émise à l'aide du bip système (\\a). Si l'outil espeak est installé, un message vocal "
        "     informe l'utilisateur du dépassement.\n\n"
        
        "  6. Arrêt de la surveillance\n"
        "     La surveillance peut être arrêtée en créant un fichier nommé stop dans le répertoire où le programme est exécuté. "
        "     Dès que ce fichier est détecté, le programme s'arrête.\n\n"
        
        "EXEMPLE D'UTILISATION\n"
        "  Lancer le programme avec un nom d'utilisateur et une adresse email :\n"
        "    ./syscpu \"christ\" \"keli@example.com\"\n\n"
        
        "  Cela affiche un message de bienvenue à christ, commence la surveillance du CPU et de la mémoire, "
        "  et envoie des alertes par email en cas de dépassement des seuils.\n\n"
        
        "ARRÊT DE LA SURVEILLANCE\n"
        "  Pour arrêter la surveillance, créez un fichier vide nommé stop dans le même répertoire que le programme :\n"
        "    touch stop\n\n"
        
        "  Le programme détecte ce fichier et s'arrête après le prochain cycle de surveillance.\n\n"
        
        "DÉPENDANCES\n"
        "  - mail : Utilisé pour envoyer des alertes par email. Assurez-vous que votre système est configuré pour envoyer des emails via cette commande.\n"
        "  - espeak (optionnel) : Utilisé pour générer des alertes vocales. Si espeak n'est pas installé, les alertes vocales seront ignorées, "
        "    mais les alertes par email et sonores fonctionneront toujours.\n\n"
        
        "RETOUR D'ÉTAT\n"
        "  Le programme génère des messages dans le terminal pour indiquer les différentes étapes du processus, y compris :\n"
        "    - Le lancement du programme avec un message de bienvenue.\n"
        "    - La surveillance continue du CPU et de la mémoire.\n"
        "    - L'envoi d'alertes par email.\n"
        "    - Le graphique de l'utilisation du CPU.\n"
        "    - Les alertes sonores et vocales lorsque les seuils sont dépassés.\n\n"
        
        "CODES DE RETOUR\n"
        "  0 : Exécution réussie du programme.\n"
        "  1 : Erreur liée aux arguments d'entrée (par exemple, si le nom ou l'email est manquant).\n"
        "  2 : Erreur lors de la création du fichier stop ou problème avec les dépendances.\n\n"
        
        "FICHIERS\n"
        "  - log.txt : Fichier journal où sont enregistrées les informations sur l'utilisation du CPU et de la mémoire à chaque cycle de surveillance.\n"
        "  - stop : Fichier utilisé pour arrêter la surveillance du programme.\n\n"
        
        "AUTEUR\n"
        "  Ce programme a été développé par christ KELI.\n\n"
    );
}

void welcome(char * name){
    print_graffiti_text();
    printf("Bonjour %s, bienvenue dans le système de surveillance CPU et mémoire !\n", name);

}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        if (argc == 2 && strcmp(argv[1], "--help") == 0) {
            doc();
            exit(0);
        }

        printf("Usage: %s <Nom> <Email>\n", argv[0]);
        printf("Plus d'nformations: ./syscpu --help\n");
        exit(1);
    }

    char *name = argv[1];
    char *email = argv[2];
    welcome(name);

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        doc();
        exit(0);
    }




    signal(SIGINT, handle_sigint); 

    pid_t pid = fork();

    if (pid < 0) {
        perror("Erreur lors de la création du processus fils");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  
        monitor_system(name, email);
        exit(EXIT_SUCCESS);
    } else {  
        printf("Processus fils lancé avec PID: %d\n", pid);
        wait(NULL);
        printf("Processus fils terminé.\n");
    }

    return 0;
}
