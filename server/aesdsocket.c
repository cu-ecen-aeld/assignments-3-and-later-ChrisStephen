#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Instructor recommended queue implementation
#include "queue.h"

// Thread Data Prototype
typedef struct thread_data_s thread_data_t;
// Thread Data Type
struct thread_data_s
{
	pthread_t id;				// Thread Identifier
	int complete;				// Thread Complete Flag
	struct sockaddr address;		// Thread Connection Address
	int fd;					// Thread File Descriptor
	SLIST_ENTRY(thread_data_s) nodes;	// Linked List Nodes
};

// Thread synchronization mutex
pthread_mutex_t mutex_fd = PTHREAD_MUTEX_INITIALIZER;

// Track signal event
volatile int signal_event = 0;

// Signal Action Handler
// 	Arguments:
// 		signal --- signal ID
// 	Returns:
// 		None
void signal_handler(int signal)
{
	// Log message with syslog that program caught signal
	syslog(LOG_INFO, "Caught signal, exiting");
	// Assert signal event
	signal_event = 1;
	return;
}

// Thread Handler
// 	Arguments:
// 		argument --- linked list node associated with thread
// 	Returns:
// 		NULL
void *thread_handler(void *argument)
{
	// Cast argument as thread data node
	thread_data_t *node = (thread_data_t *)argument;
	// Storage of rollover data
	char *rollover = malloc((1024 + 1) * sizeof(char));
	// Check for error condition
	if (rollover == NULL)
	{
		syslog(LOG_DEBUG, "rollover malloc() error");
		// Mark node as complete
		node->complete = 1;
		// Exit as result of error condition
		return NULL;
	}
	// Wipe rollover
	memset(rollover, '\0', (1024 + 1) * sizeof(char));
	// Storage of buffer data
	char *buffer = NULL;
	// Size of buffer data
	int size = 0;
	// Loop until signal event asserted
	while (signal_event == 0)
	{
		// Log message with syslog that program opened connection
		syslog(LOG_INFO, "Accepted connection from %d.%d.%d.%d", node->address.sa_data[2], node->address.sa_data[3], node->address.sa_data[4], node->address.sa_data[5]);
		// Assign initial size of buffer data
		size = strnlen(rollover, 1024);
		// Allocate initial storage of buffer data
		buffer = malloc(((((size / 1024) + 1) * 1024) + 1) * sizeof(char));
		// Check for error condition
		if (buffer == NULL)
		{
			syslog(LOG_DEBUG, "buffer malloc() error");
			// Exit as result of error condition
			break;
		}
		// Duplicate contents from rollover to buffer
		strncpy(buffer, rollover, 1024);
		// Confirm existence of null-terminator for string operations
		buffer[size] = '\0';
		// Wipe rollover
		memset(rollover, '\0', (1024 + 1) * sizeof(char));
		// Loop until newline found or signal event asserted
		while ((strchr(buffer, '\n') == NULL) && (signal_event == 0))
		{
			// Check number of characters recieved
			int characters = recv(node->fd, &buffer[size], 1024 - (size % 1024), 0);
			// Check for error condition
			if (characters == -1)
			{
				syslog(LOG_DEBUG, "recv() error");
				// Exit as result of error condition
				break;
			}
			// Update size with number of characters
			size += characters;
			// Confirm existence of null-terminator for string operations
			buffer[size] = '\0';
			// Reallocate buffer whenever full
			if (size % 1024 == 0)
			{
				// Update storage with reallocation of memory
				buffer = realloc(buffer, ((((size / 1024) + 1) * 1024) + 1) * sizeof(char));
				// Check for error condition
				if (buffer == NULL)
				{
					syslog(LOG_DEBUG, "buffer realloc() error");
					// Exit as result of error condition
					break;
				}
			}
		}
		// Track spillover
		char *spillover = buffer;
		// Track newline
		char *newline = buffer;
		// Loop while newline can be found and signal event not asserted
		while ((newline != NULL) && (signal_event == 0))
		{
			// Lock mutex
			pthread_mutex_lock(&mutex_fd);
			// Find '\n' character in string
			newline = strchr(spillover, '\n');
			// Check that newline is found
			if (newline != NULL)
			{
				// Status check
				int rc = 0;
				// Track descriptor
				int fd;
				// Track number of bytes read/write
				int bytes;
				// Open file (create as necessary, write-only, append mode)
				fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_APPEND, 0666);
				// Check for error condition
				if (fd == -1)
				{
					syslog(LOG_DEBUG, "write /var/tmp/aesdsocketdata open() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Write string
				bytes = write(fd, spillover, (newline - spillover) + 1);
				// Check for error condition
				if (bytes == -1)
				{
					syslog(LOG_DEBUG, "write() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Close file
				rc = close(fd);
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "write /var/tmp/aesdsocketdata close() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Allocate message
				char *message = malloc(1024 * sizeof(char));
				// Check for error condition
				if (message == NULL)
				{
					syslog(LOG_DEBUG, "message malloc() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Open file (read-only)
				fd = open("/var/tmp/aesdsocketdata", O_RDONLY, 0666);
				// Check for error condition
				if (fd == -1)
				{
					syslog(LOG_DEBUG, "read /var/tmp/aesdsocketdata open() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Chunk message string as 1024 characters
				bytes = 1024;
				// Loop until EOF or signal event asserted
				while ((bytes == 1024) && (signal_event == 0))
				{
					// Read string
					bytes = read(fd, message, 1024);
					// Check for error condition
					if (bytes == -1)
					{
						syslog(LOG_DEBUG, "read() error");
						// Unlock mutex
						pthread_mutex_unlock(&mutex_fd);
						// Exit as result of error condition
						break;
					}
					// Check number of characters sent
					int characters = send(node->fd, message, bytes, 0);
					// Check for error condition
					if (characters == -1)
					{
						syslog(LOG_DEBUG, "send() error");
						// Unlock mutex
						pthread_mutex_unlock(&mutex_fd);
						// Exit as result of error condition
						break;
					}
				}
				// Close file
				rc = close(fd);
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "read /var/tmp/aesdsocketdata close() error");
					// Unlock mutex
					pthread_mutex_unlock(&mutex_fd);
					// Exit as result of error condition
					break;
				}
				// Free message
				free(message);
				// Update spillover as character following newline
				spillover = newline + 1;
			}
			// Unlock mutex
			pthread_mutex_unlock(&mutex_fd);
		}
		// Duplicate contents from spillover to rollover
		strncpy(rollover, spillover, 1024);
		// Free buffer
		free(buffer);
		// Log message with syslog that program closed connection
		syslog(LOG_INFO, "Closed connection from %d.%d.%d.%d", node->address.sa_data[2], node->address.sa_data[3], node->address.sa_data[4], node->address.sa_data[5]);
	}
	// Free rollover
	free(rollover);
	// Mark node as complete
	node->complete = 1;
	// Exit thread
	pthread_exit(NULL);
	// Return NULL
	return NULL;
}

// Timestamp Handler
// 	Arguments:
// 		argument --- linked list node associated with thread
// 	Returns:
// 		NULL
void *timestamp_handler(void *argument)
{
	// Cast argument as thread data node
	thread_data_t *node = (thread_data_t *)argument;
	// Timestamp format
	const char *format = "timestamp:%a, %d %b %Y %T %z\n";
	// Epoch time
	time_t epoch;
	// Current time
	struct tm *current;
	// Timestamp string
	char string[256];
	// Timestamp characters
	size_t characters;
	// Open file (create as necessary, append mode)
	int fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_APPEND, 0666);
	// Loop until signal event asserted
	while (signal_event == 0)
	{
		// Sleep 10 Seconds
		sleep(10);
		// Lock mutex
		pthread_mutex_lock(&mutex_fd);
		// Grab epoch time
		epoch = time(NULL);
		// Convert as current time
		current = localtime(&epoch);
		// Create timestamp
		characters = strftime(string, sizeof(string), format, current);
		// Print timestamp
		write(fd, string, characters);
		// Unlock mutex
		pthread_mutex_unlock(&mutex_fd);

	}
	// Close file
	close(fd);
	// Mark node as complete
	node->complete = 1;
	// Exit thread
	pthread_exit(NULL);
	// Return NULL
	return NULL;
}

// Main
// 	Arguments:
// 		argc --- number of arguments
// 		argv --- pointers of arguments
// 	Returns:
// 		State
int main(int argc, char **argv)
{
	// Track return codes
	int rc = 0;
	// Configure signal action with signal handler
	struct sigaction action = { 0 };
	action.sa_handler = signal_handler;
	// Assign signal action for SIGINT
	rc = sigaction(SIGINT, &action, NULL);
	// Check for error condition
	if (rc != 0)
	{
		syslog(LOG_DEBUG, "SIGINT sigaction() error");
		// Exit as result of error condition
		return rc;
	}
	// Assign signal action for SIGTERM
	rc = sigaction(SIGTERM, &action, NULL);
	// Check for error condition
	if (rc != 0)
	{
		syslog(LOG_DEBUG, "SIGTERM sigaction() error");
		// Exit as result of error condition
		return rc;
	}
	// Configure hints
	struct addrinfo hints;
	hints.ai_flags = AI_PASSIVE;		// Passive
	hints.ai_family = AF_INET;		// IPv4
	hints.ai_socktype = SOCK_STREAM;	// TCP
	hints.ai_protocol = 0;			// Any
	// Configure result
	struct addrinfo *result;
	// Network address and service translation --- allocate memory
	rc = getaddrinfo(NULL, "9000", &hints, &result);
	// Check for error condition
	if (rc != 0)
	{
		syslog(LOG_DEBUG, "getaddrinfo() error");
		// Exit as result of error condition
		return rc;
	}
	// Create an endpoint for communication
	int sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	// Check for error condition
	if (sfd == -1)
	{
		syslog(LOG_DEBUG, "socket() error");
		// Exit as result of error condition
		return sfd;
	}
	// Bind name with socket
	rc = bind(sfd, result->ai_addr, result->ai_addrlen);
	// Check for error condition
	if (rc != 0)
	{
		syslog(LOG_DEBUG, "bind() error");
		// Exit as result of error condition
		return rc;
	}
	// Network address and service translation --- free memory
	freeaddrinfo(result);
	// Check whether argument specified
	if (argc > 1)
	{
		// Check whether argument specified matches '-d' for daemon mode
		if (strcmp(argv[1], "-d") == 0)
		{
			// Fork process for daemon mode
			int pid = fork();
			// Check process ID
			if (pid != 0)	// Parent process
			{
				// Exit as result of process ID check
				return 0;
			}
			else		// Child process
			{
				// Create session and assign process ID for daemon
				int sid = setsid();
				// Check for error condition
				if (sid == -1)
				{
					syslog(LOG_DEBUG, "setsid() error");
					// Exit as result of error condition
					exit(sid);
				}
				// Change working path as root
				rc = chdir("/");
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "chdir() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Close STDIN
				rc = close(0);
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "STDIN close() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Open /dev/null as STDIN
				rc = open("/dev/null", O_RDONLY);
				// Check for error condition (not STDIN fd 0)
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "STDIN open() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Close STDOUT
				rc = close(1);
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "STDOUT close() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Open /dev/null as STDOUT
				rc = open("/dev/null", O_WRONLY);
				// Check for error condition (not STDOUT fd 1)
				if (rc != 1)
				{
					syslog(LOG_DEBUG, "STDOUT open() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Close STDERR
				rc = close(2);
				// Check for error condition
				if (rc != 0)
				{
					syslog(LOG_DEBUG, "STDERR close() error");
					// Exit as result of error condition
					exit(rc);
				}
				// Open /dev/null as STDERR
				rc = open("/dev/null", O_RDWR);
				// Check for error condition (not STDERR fd 2)
				if (rc != 2)
				{
					syslog(LOG_DEBUG, "STDERR open() error");
					// Exit as result of error condition
					exit(rc);
				}
			}

		}
	}
	// Create linked list
	SLIST_HEAD(linked_list_s, thread_data_s) base_node;
	// Initialize base node
	SLIST_INIT(&base_node);
	// Create new linked list node
	thread_data_t *timestamp_node = malloc(sizeof(thread_data_t));
	// Check for error condition
	if (timestamp_node == NULL)
	{
		syslog(LOG_DEBUG, "timestamp_node malloc() error");
		// Exit as result of error condition
		exit(-1);
	}
	// Initialize node as ID 0
	timestamp_node->id = 0;
	// Initialize node as not complete
	timestamp_node->complete = 0;
	// Create new thread
	pthread_create(&timestamp_node->id, NULL, timestamp_handler, (void *)timestamp_node);
	// Insert node to linked list
	SLIST_INSERT_HEAD(&base_node, timestamp_node, nodes);
	// Track socket address information
	struct sockaddr address = { 0 };
	// Track socket address length
	socklen_t address_length = sizeof(address);
	// Loop until signal event asserted
	while (signal_event == 0)
	{
		// Listen for connections on socket
		rc = listen(sfd, 1);
		// Check for error condition
		if (rc != 0)
		{
			syslog(LOG_DEBUG, "listen() error");
			// Check for signal event
			if (signal_event != 0)
			{
				break;
			}
			// Exit as result of error condition
			exit(rc);
		}
		// Accept connection on socket
		int cfd = accept(sfd, &address, &address_length);
		// Check for error condition
		if (cfd == -1)
		{
			syslog(LOG_DEBUG, "accept() error");
			if (signal_event != 0)
			{
				break;
			}
			// Exit as result of error condition
			exit(cfd);
		}
		// Create new linked list node
		thread_data_t *node = malloc(sizeof(thread_data_t));
		// Check for error condition
		if (node == NULL)
		{
			syslog(LOG_DEBUG, "node malloc() error");
			// Exit as result of error condition
			exit(-1);
		}
		// Initialize node as ID 0
		node->id = 0;
		// Initialize node as not complete
		node->complete = 0;
		// Initialize node connection address
		memcpy(&node->address, &address, sizeof(struct sockaddr));
		// Initialize node file descriptor
		node->fd = cfd;
		// Create new thread
		pthread_create(&node->id, NULL, thread_handler, (void *)node);
		// Insert node to linked list
		SLIST_INSERT_HEAD(&base_node, node, nodes);
		// Sleep 0.5 seconds
		sleep(0.5);
		// Create iterable
		thread_data_t *iterable;
		// Iterate through nodes of linked list
		SLIST_FOREACH(iterable, &base_node, nodes)
		{
			// Check for thread complete flag
			if (iterable->complete != 0)
			{
				// Remove node from linked list
				SLIST_REMOVE(&base_node, iterable, thread_data_s, nodes);
				// Join completed thread
				pthread_join(iterable->id, NULL);
				// Free completed linked list node
				free(iterable);
				// Break iteration because iterable has been modified
				break;
			}
		}
	}
	// Track current node
	thread_data_t *current_node = NULL;
	// Iterate through all nodes of linked list
	while (!SLIST_EMPTY(&base_node))
	{
		// Grab current node
		current_node = SLIST_FIRST(&base_node);
		// Remove linked list node
		SLIST_REMOVE_HEAD(&base_node, nodes);
		// Sleep 0.5 seconds
		sleep(0.5);
		// Terminate linked list node thread
		pthread_kill(current_node->id, SIGTERM);
		// Join linked list node thread
		pthread_join(current_node->id, NULL);
		// Free linked list node
		free(current_node);
	}
	// Remove /var/tmp/aesdsocketdata
	remove("/var/tmp/aesdsocketdata");
	return 0;
}
