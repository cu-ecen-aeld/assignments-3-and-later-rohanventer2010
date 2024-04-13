#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>
#include <libgen.h>
#include <fcntl.h>

/* function prototypes */
void signal_handler(int);
void uint32_to_ip(uint32_t, char *);
void safe_shutdown(void);
int find_chr_in_str(const char*, int, char);

/* constants */
const uint16_t DEFAULT_PORT = 9000;
const char *TEMP_FILE = "/var/tmp/aesdsocketdata\0";

/* globals */
bool gracefull_exit = false;
int server_fd = -1;
int accepted_fd = -1;
int tempfile_fd = -1;
char *big_buffer = NULL;

/* program entry point */
int main(int argc, char *argv[])
{
  int ret = -1; /* generic return result */
  bool daemon_flag = false;
  uint16_t socket_port = DEFAULT_PORT;  

  int opt = -1;
  while ((opt = getopt(argc, argv, "p:d")) != -1) {
    switch (opt) {
      case 'p':
        socket_port = (uint16_t)strtol(optarg, NULL, 10);
        break;
      case 'd':
        daemon_flag = true;
        break;              
      case '?':
        printf("Unknown option or missing argument\n");
        exit(EXIT_FAILURE);
      default:
        printf("Unexpected result from getopt\n");
        exit(EXIT_FAILURE);
    }
  }

  /* get the program name, as it was called */
  char *base_name;
  base_name = basename(argv[0]);

  /* set up logging */
  openlog(base_name, LOG_PID, LOG_USER);
  setlogmask(LOG_UPTO(LOG_DEBUG));

  remove(TEMP_FILE);

  /* Opens a stream socket bound to port 9000, failing and returning -1 if any of the socket connection steps fail.
  *
  * int socket(int domain, int type, int protocol);
  * On success, a file descriptor for the new socket is returned.  On error, -1 is returned
  */
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) 
  {
    syslog(LOG_ERR, "Socket could not be created");
    safe_shutdown();
    exit(EXIT_FAILURE);
  }

  /*
  * int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);
  * Upon successful completion, setsockopt() shall return 0. Otherwise, -1 shall be returned
  */
  int option_value = 1;
  ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value));
  if (ret < 0) 
  {
    syslog(LOG_ERR, "Socket options could not be set");
    safe_shutdown();
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in socket_address;
  socket_address.sin_family = AF_INET;
  socket_address.sin_addr.s_addr = INADDR_ANY;
  socket_address.sin_port = htons(socket_port);

  /* 
  *  int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen); 
  *  On success, zero is returned.  On error, -1 is returned
  */
  ret = bind(server_fd, (struct sockaddr*)&socket_address, sizeof(socket_address));
  if (ret < 0) 
  {
    syslog(LOG_ERR, "Socket could bind to address/port");
    safe_shutdown();
    exit(EXIT_FAILURE);
  }

  /*
  * int listen(int sockfd, int backlog);
  * On success, zero is returned.  On error, -1 is returned
  */
  ret = listen(server_fd, 10);
  if (ret < 0)
  {
    syslog(LOG_ERR, "Socket could not listen");
    safe_shutdown();
    exit(EXIT_FAILURE);
  }
  /* catch signal SIGINT and SIGTERM 
  * setting up the signal handlers should happen afeter daemonizing
  */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);


  /* lets daemonize if needed */
  if (daemon_flag)
  {
    pid_t pid;

    /* Fork the parent process */
    pid = fork();

    /* Exit parent process */
    if (pid < 0)
        exit(EXIT_FAILURE);

    if (pid > 0)
      exit(EXIT_SUCCESS);

    /* Create new session */
    if (setsid() < 0)
      exit(EXIT_FAILURE);

    /* Fork again */
    pid = fork();
    if (pid < 0)
      exit(EXIT_FAILURE);

    if (pid > 0) {
      /* Print the PID of the second child process and exit */
      printf("Daemon PID: %d\n", pid);
      exit(EXIT_SUCCESS);
    }

    /* Change working directory */
    if (chdir("/") < 0)
      exit(EXIT_FAILURE);

    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  while (1)
  {
    /* int accept(int sockfd, struct sockaddr *_Nullable restrict addr, socklen_t *_Nullable restrict addrlen);
    * On success, these system calls return a file descriptor for the
    *   accepted socket (a nonnegative integer).  On error, -1 is returned
    */
    printf("Listening for connections on port %d...\n", socket_port);
    socklen_t addrlen = sizeof(socket_address);
    accepted_fd = accept(server_fd, (struct sockaddr*)&socket_address, &addrlen);
    if (accepted_fd < 0)
    {
      syslog(LOG_ERR, "Socket could not accept");
      exit(EXIT_FAILURE);
    }

    char ip_str[16];
    struct in_addr sin_addr = socket_address.sin_addr;
    uint32_to_ip(sin_addr.s_addr, ip_str);
    syslog(LOG_DEBUG, "Accepted connection from %s", ip_str); 

    /* get bytes from socket */
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = -1;
    while (1)
    {
      while (1)
      {
        /* loop recieving data and writing to file until we get a '\n' */

        /* ssize_t recv(int sockfd, void *buf, size_t len, int flags); 
        * The recv() function returns the number of bytes received, or -1 if an error occurred
        */
        bytes_received = recv(accepted_fd, buffer, sizeof(buffer), 0);
        if (bytes_received < 0)
        {
          syslog(LOG_ERR, "Error ocurred recieving data");
          safe_shutdown();
          exit(EXIT_FAILURE);
        }

        if (bytes_received == 0)
          break; /* connection closed by peer */

        /* write data to the temp file and then close */
        tempfile_fd = open(TEMP_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666);
        if (tempfile_fd < 0)
        {
          syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
          safe_shutdown();
          exit(EXIT_FAILURE);
        }

        /* find position in '\n' if any*/
        int pos = find_chr_in_str((const char *)buffer, bytes_received, '\n');
        if (pos < 0)
        {
          /* '\n' was not found, write entire buffer */
          ret = write(tempfile_fd, buffer, bytes_received);
          if (ret < 0)
          {
            syslog(LOG_ERR, "Could not write to temp file %s, '\\n' was not found", TEMP_FILE);
            safe_shutdown();
            exit(EXIT_FAILURE);        
          }
          close(tempfile_fd);        
        }
        else
        {
          /* '\n' was found, write only upto returned position */
          ret = write(tempfile_fd, buffer, pos+1);
          if (ret < 0)
          {
            syslog(LOG_ERR, "Could not write to temp file %s, '\\n' was found", TEMP_FILE);
            safe_shutdown();
            exit(EXIT_FAILURE);        
          }
          close(tempfile_fd);
          break; 
        }
      } /* recv */

      if (bytes_received == 0)
      {
        syslog(LOG_INFO, "Closed connection from %s", ip_str);
        close(accepted_fd);
        break;
      }
      else
      {
        /* open the temp file again and read all data and send back to remote peer */
        tempfile_fd = open(TEMP_FILE, O_RDONLY);
        if (tempfile_fd < 0)
        {
          syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
          safe_shutdown();
          exit(EXIT_FAILURE);
        }   

        char file_buffer[1024];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(tempfile_fd, file_buffer, sizeof(file_buffer))) > 0)
        {
          send(accepted_fd, file_buffer, bytes_read, 0);
        }
        close(tempfile_fd);
      } /* if bytes_received == 0*/
    } /* recv, write and send */
  } /* accept */

  printf("exit normally - should never get here\n");
  return 0;
}

/* convert the utin32_t to an readable IP address */
void uint32_to_ip(uint32_t ip, char *ip_str) 
{
  // Extract each byte of the IPv4 address
  unsigned char *bytes = (unsigned char *)&ip;

  // Format the IPv4 address into dotted-decimal notation
  sprintf(ip_str, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
}


/* signal handler */
void signal_handler(int signum)
{
  if (signum == SIGINT || signum == SIGTERM)
  {
    safe_shutdown();
    exit(EXIT_SUCCESS);
  }
}

void safe_shutdown(void)
{
  if (accepted_fd >= 0)
    close(accepted_fd);

  if (server_fd >= 0)
    close(server_fd);

  if (tempfile_fd >= 0)
    close(tempfile_fd);    

  /* int remove(const char *filename) 
  * On success, zero is returned. On error, -1 is returned
  *
  * we dont really care if error ocurred here?
  */
  remove(TEMP_FILE);
  
}

int find_chr_in_str(const char *str, int str_len, char c)
{
  int ii;
  for (ii = 0; ii < str_len; ii++)
  {
    char b = str[ii];
    if ( b == c)
      return ii;
  }

  return -1;
}