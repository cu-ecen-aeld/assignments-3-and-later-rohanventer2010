
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
//#include <siginfo.h>
#include <time.h>
#include <syslog.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <libgen.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>

#include "threading.h"
#include "queue.h"
#include "aesd_ioctl.h"

/* function prototypes */
void signal_handler(int);
void uint32_to_ip(uint32_t, char *);
void safe_shutdown(void);
int find_chr_in_str(const char*, int, char);
void* socket_thread_func(void*);
void* timer_thread_func(void*);

/* Forward declaration of struct sigevent */
struct sigevent;

/* constants */
const uint16_t DEFAULT_PORT = 9000;
//const char *TEMP_FILE = "/var/tmp/aesdsocketdata";

#ifdef USE_AESD_CHAR_DEVICE
#define TEMP_FILE "/dev/aesdchar"
#else
#define TEMP_FILE "/var/tmp/aesdsocketdata"
#endif

/* structs */
struct thread_entry
{
  pthread_t thread_id;
  struct socket_thread_data *thread_data;
  SLIST_ENTRY(thread_entry) threads;
};

/* globals */
int server_fd = -1;
struct thread_entry *thread_list_entry = NULL;
struct slisthead head;
pthread_t timer_thread_id = -1;

SLIST_HEAD(slisthead, thread_entry);

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

#ifndef USE_AESD_CHAR_DEVICE
  remove(TEMP_FILE);
#endif


  /* inititialize mutex */
  pthread_mutex_t mutex;
  ret = pthread_mutex_init(&mutex, NULL);
  if (ret != 0) 
  {
    syslog(LOG_ERR, "Mutex cannot be initialized");
    exit(EXIT_FAILURE);
  }
  //TODO: mutex_destroy

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


  
  SLIST_INIT(&head);
#ifndef USE_AESD_CHAR_DEVICE
  /* setup timer thread */ 
  struct timer_thread_data timer_thread_func_args;
  timer_thread_func_args.mutex = &mutex;

  ret = pthread_create(&timer_thread_id, NULL, timer_thread_func, &timer_thread_func_args);
  if(ret != 0)
  {
    syslog(LOG_ERR, "Error creating thread");
    safe_shutdown();
    exit(EXIT_FAILURE);
  }
#endif

  printf("Listening for connections on port %d...\n", socket_port);
  while (1)
  {
    /* int accept(int sockfd, struct sockaddr *_Nullable restrict addr, socklen_t *_Nullable restrict addrlen);
    * On success, these system calls return a file descriptor for the
    *   accepted socket (a nonnegative integer).  On error, -1 is returned
    */
    
    socklen_t addrlen = sizeof(socket_address);
    int accepted_fd = accept(server_fd, (struct sockaddr*)&socket_address, &addrlen);
    if (accepted_fd < 0)
    {
      syslog(LOG_ERR, "Socket could not accept");
      //TODO: better exit routine?
      exit(EXIT_FAILURE);
    }

    char ip_str[16];
    struct in_addr sin_addr = socket_address.sin_addr;
    uint32_to_ip(sin_addr.s_addr, ip_str);
    syslog(LOG_DEBUG, "Accepted connection from %s", ip_str); 
    printf("Accepted connection from %s\n", ip_str); 
    
    /* the threading stuff */
    thread_list_entry = NULL;
    thread_list_entry = malloc(sizeof(struct thread_entry));
    if (thread_list_entry != NULL)
    {
      // error out because malloc failed
      //TODO: safe shutdown
    }    

    /* setup data structure */
    struct socket_thread_data *thread_func_args = malloc(sizeof(struct socket_thread_data));
    if (thread_func_args != NULL)
    {
      // error out because malloc failed
      //TODO: safe shutdown
    }
    thread_func_args->mutex = &mutex;
    thread_func_args->accepted_fd = accepted_fd;
    thread_func_args->thread_completed = false;
    thread_func_args->thread_generated_error = false;
    strncpy(thread_func_args->ip_str, ip_str, 16);

    thread_list_entry->thread_data = thread_func_args;

    ret = pthread_create(&(thread_list_entry->thread_id), NULL, socket_thread_func, thread_func_args);
    if(ret != 0)
    {
      syslog(LOG_ERR, "Error creating thread");
      free(thread_list_entry->thread_data);
      free(thread_list_entry);
      safe_shutdown();
      exit(EXIT_FAILURE);
    }

    SLIST_INSERT_HEAD(&head, thread_list_entry, threads);
    
    thread_list_entry = NULL;
    struct thread_entry *thead_entry_temp = NULL;
    SLIST_FOREACH_SAFE(thread_list_entry, &head, threads, thead_entry_temp)
    {
      if (thread_list_entry->thread_data->thread_completed)
      {
        pthread_join(thread_list_entry->thread_id, NULL);
        SLIST_REMOVE(&head, thread_list_entry, thread_entry, threads);
        free(thread_list_entry->thread_data);
        free(thread_list_entry); 
      }
    }
    free(thead_entry_temp); // this?

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
  //if (accepted_fd >= 0)
  //  close(accepted_fd);
  struct thread_entry *n1 = NULL;

  if (server_fd >= 0)
    close(server_fd);

  pthread_cancel(timer_thread_id);
  pthread_join(timer_thread_id, NULL);

  while (!SLIST_EMPTY(&head)) {           /* List Deletion. */
    n1 = SLIST_FIRST(&head);
    pthread_cancel(n1->thread_id);
    pthread_join(n1->thread_id, NULL);
    SLIST_REMOVE_HEAD(&head, threads);
    free(n1->thread_data);
    free(n1);
  }


  //if (tempfile_fd >= 0)
  //  close(tempfile_fd);    

  /* int remove(const char *filename) 
  * On success, zero is returned. On error, -1 is returned
  *
  * we dont really care if error ocurred here?
  */
#ifndef USE_AESD_CHAR_DEVICE
  remove(TEMP_FILE);
#endif
  
}

int find_chr_in_str(const char *str, int str_len, char c)
{
  int ii;
  for (ii = 0; ii < str_len; ii++)
  {
    char b = str[ii];
    if (b == c)
      return ii;
  }

  return -1;
}

void* socket_thread_func(void* thread_param)
{
  char recv_buffer[1024];
  //memset(buffer, 0, sizeof(buffer));
  ssize_t bytes_received = -1;
  int tempfile_fd = -1;

  struct socket_thread_data* thread_func_args = (struct socket_thread_data *) thread_param;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  while(1)
  {
    int ret;
    ret = pthread_mutex_lock(thread_func_args->mutex);
    if (ret != 0)
    {
      syslog(LOG_ERR, "Error acquiring mutex");
      if (thread_func_args->accepted_fd >= 0)
        close(thread_func_args->accepted_fd);      
      thread_func_args->thread_completed = true;
      thread_func_args->thread_generated_error = true;    
      return thread_param;
    }

    /* open the file */
    tempfile_fd = open(TEMP_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666);
    if (tempfile_fd < 0)
    {
      syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
      if (thread_func_args->accepted_fd >= 0)
        close(thread_func_args->accepted_fd);      
      thread_func_args->thread_completed = true;
      thread_func_args->thread_generated_error = true;
      pthread_mutex_unlock(thread_func_args->mutex);
      return thread_param;
    }

    while (1)
    {
      bytes_received = recv(thread_func_args->accepted_fd, recv_buffer, sizeof(recv_buffer), 0);
      if (bytes_received < 0)
      {
        syslog(LOG_ERR, "Error ocurred recieving data");
        if (thread_func_args->accepted_fd >= 0)
          close(thread_func_args->accepted_fd);      
        thread_func_args->thread_completed = true;
        thread_func_args->thread_generated_error = true;
        return thread_param;
      }

      if (bytes_received == 0)
        break; /* connection closed by peer */

      // int ret;
      // ret = pthread_mutex_lock(thread_func_args->mutex);
      // if (ret != 0)
      // {
      //   syslog(LOG_ERR, "Error acquiring mutex");
      //   if (thread_func_args->accepted_fd >= 0)
      //     close(thread_func_args->accepted_fd);      
      //   thread_func_args->thread_completed = true;
      //   thread_func_args->thread_generated_error = true;    
      //   return thread_param;
      // }

      // /* open the file */
      // tempfile_fd = open(TEMP_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666);
      // if (tempfile_fd < 0)
      // {
      //   syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
      //   if (thread_func_args->accepted_fd >= 0)
      //     close(thread_func_args->accepted_fd);      
      //   thread_func_args->thread_completed = true;
      //   thread_func_args->thread_generated_error = true;
      //   pthread_mutex_unlock(thread_func_args->mutex);
      //   return thread_param;
      // }

      /* string sent over the socket equals AESDCHAR_IOCSEEKTO:X,Y 
       * where X and Y are unsigned decimal integer values, 
       * the X should be considered the write command to seek into and 
       * the Y should be considered the offset within the write command */
#ifdef USE_AESD_CHAR_DEVICE      
      uint32_t write_cmd;
      uint32_t write_cmd_offset;
      struct aesd_seekto seekto = {
        .write_cmd = 0,
        .write_cmd_offset = 0
      };
      ret = sscanf(recv_buffer, "AESDCHAR_IOCSEEKTO:%u,%u\n", &write_cmd, &write_cmd_offset);
      if (ret == 2) /* we need to get 2 parameters */
      {
        seekto.write_cmd = write_cmd;
        seekto.write_cmd_offset = write_cmd_offset;
        ret = ioctl(tempfile_fd, AESDCHAR_IOCSEEKTO, &seekto);
        if (ret != 0)
        {
          syslog(LOG_ERR, "Could not ioctl, write_cmd: %u, write_cmd_offset: %u", write_cmd, write_cmd_offset);
          if (thread_func_args->accepted_fd >= 0)
            close(thread_func_args->accepted_fd);      
          thread_func_args->thread_completed = true;
          thread_func_args->thread_generated_error = true;
          pthread_mutex_unlock(thread_func_args->mutex);
          return thread_param;
        }
        //close(tempfile_fd);
        break;
      }
      else
#endif      
      {
        // ret = pthread_mutex_lock(thread_func_args->mutex);
        // if (ret != 0)
        // {
        //   syslog(LOG_ERR, "Error acquiring mutex");
        //   if (thread_func_args->accepted_fd >= 0)
        //     close(thread_func_args->accepted_fd);      
        //   thread_func_args->thread_completed = true;
        //   thread_func_args->thread_generated_error = true;    
        //   return thread_param;
        // }

        /* write data to the temp file and then close */
        // tempfile_fd = open(TEMP_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666);
        // if (tempfile_fd < 0)
        // {
        //   syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
        //   if (thread_func_args->accepted_fd >= 0)
        //     close(thread_func_args->accepted_fd);      
        //   thread_func_args->thread_completed = true;
        //   thread_func_args->thread_generated_error = true;
        //   pthread_mutex_unlock(thread_func_args->mutex);
        //   return thread_param;
        // }

        /* find position in '\n' if any*/
        int pos = find_chr_in_str((const char *)recv_buffer, bytes_received, '\n');
        if (pos < 0)
        {
          /* '\n' was not found, write entire buffer */
          ret = write(tempfile_fd, recv_buffer, bytes_received);
          if (ret < 0)
          {
            syslog(LOG_ERR, "Could not write to temp file %s, '\\n' was not found", TEMP_FILE);
            if (thread_func_args->accepted_fd >= 0)
              close(thread_func_args->accepted_fd);        
            thread_func_args->thread_completed = true;
            thread_func_args->thread_generated_error = true;
            pthread_mutex_unlock(thread_func_args->mutex);      
            return thread_param;
          }
          //close(tempfile_fd);        
        }
        else
        {
          /* '\n' was found, write only upto returned position */
          ret = write(tempfile_fd, recv_buffer, pos+1);
          if (ret < 0)
          {
            syslog(LOG_ERR, "Could not write to temp file %s, '\\n' was found", TEMP_FILE);
            if (thread_func_args->accepted_fd >= 0)
              close(thread_func_args->accepted_fd);        
            thread_func_args->thread_completed = true;
            thread_func_args->thread_generated_error = true;
            pthread_mutex_unlock(thread_func_args->mutex);
            return thread_param;
          }
          //close(tempfile_fd);
          break; 
        }
      }
    }

    if (bytes_received == 0)
    {
      syslog(LOG_INFO, "Closed connection from %s", thread_func_args->ip_str);
      printf("Closed connection from %s\n", thread_func_args->ip_str);
      if (thread_func_args->accepted_fd >= 0)
        close(thread_func_args->accepted_fd);
      thread_func_args->thread_completed = true;
      thread_func_args->thread_generated_error = false;
      pthread_mutex_unlock(thread_func_args->mutex);
      close(tempfile_fd);
      return thread_param;
    }
    else
    {
      /* open the temp file again and read all data and send back to remote peer */
      printf("Read from circular buffer");
      // tempfile_fd = open(TEMP_FILE, O_RDONLY);
      // if (tempfile_fd < 0)
      // {
      //   syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);
      //   if (thread_func_args->accepted_fd >= 0)
      //     close(thread_func_args->accepted_fd);
      //   thread_func_args->thread_completed = true;
      //   thread_func_args->thread_generated_error = true;
      //   pthread_mutex_unlock(thread_func_args->mutex);
      //   return thread_param;
      // }   

      char file_buffer[1024];
      ssize_t bytes_read = 0;
      while ((bytes_read = read(tempfile_fd, file_buffer, sizeof(file_buffer))) > 0)
      {
        send(thread_func_args->accepted_fd, file_buffer, bytes_read, 0);
      }
      close(tempfile_fd);
      pthread_mutex_unlock(thread_func_args->mutex);
    } /* if bytes_received == 0*/
  }

  if (thread_func_args->accepted_fd >= 0)
    close(thread_func_args->accepted_fd);

  thread_func_args->thread_completed = true;
  thread_func_args->thread_generated_error = false;
  pthread_mutex_unlock(thread_func_args->mutex);
  return thread_param;
}

void* timer_thread_func(void* thread_param)
{
  int timer_count = 0;
  int tempfile_fd = -1;
  int ret = -1;
  struct timer_thread_data* thread_func_args = (struct timer_thread_data *) thread_param;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  
  while(1)
  {
    if (timer_count < 10)
    {
      timer_count++;
      usleep(1000000);
    }
    else
    {
      timer_count = 0;
      ret = pthread_mutex_lock(thread_func_args->mutex);
      if (ret != 0)
      {
        syslog(LOG_ERR, "Error acquiring mutex");     
        return thread_param;
      }

      tempfile_fd = open(TEMP_FILE, O_CREAT | O_APPEND | O_WRONLY, 0666);
      if (tempfile_fd < 0)
      {
        syslog(LOG_ERR, "Could not open temp file %s", TEMP_FILE);     
        pthread_mutex_unlock(thread_func_args->mutex);
        return thread_param;
      }

      time_t t = time(NULL);
      char time_str[50];
      // year, month, day, hour (in 24 hour format) minute and second
      strftime(time_str, 100, "timestamp:%Y, %m, %d, %H, %M, %S\n", localtime(&t));
      ret = write(tempfile_fd, time_str, strlen(time_str));
      if (ret < 0)
      {
        syslog(LOG_ERR, "Could not write to temp file %s.", TEMP_FILE);
        if (tempfile_fd >= 0)
          close(tempfile_fd);
        pthread_mutex_unlock(thread_func_args->mutex);
        return thread_param;
      }
      close(tempfile_fd);
      pthread_mutex_unlock(thread_func_args->mutex);
    }

  }

}

