/** @file code.c
 *  @brief Implement communication to Google IoT cloud using the google IoT embedded C SDK
 *  Compiled using the steps given in https://github.com/GoogleCloudPlatform/iot-device-sdk-embedded-c/tree/master/examples/iot_core_mqtt_client
 *  @author Plabini Jibanjyoti Nayak
 *  @bug No bugs found
 */

/**-------------------------------------------------------------------------------
                        ## Function Description ##
*--------------------------------------------------------------------------------
* iotc_command_line() - Parse arguments for the options specified in the options 
* string and check for missing parameters. Implements a command line argument 
* parser, defined in commandline.c
*
* load_key() - Function to load key.
*
* rec_message() - Function to print received message on the clients's subscribed
* topic. This is part of subsciption callback.
*
* on_connection_state_changed() - Check the connection state and perform the required 
* tasks such as handling of such as successful and unsuccessful connection.
*
* publish() -Function to publish stored in global opts struture which is passed in 
* as payload to Cloud IOT Core.
* pass_key() - This example assumes the private key to be used to  sign the IoT Core. 
* Connect JWT credential is a PEM encoded ES256 private key, and passes it IoT Core 
* Device Client functions as a byte array.
*
* main() - Main function.
*-----------------------------------------------------------------------------------
*/

/* --- Standard Includes --- */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <iotc.h>
#include <iotc_jwt.h>
#include <iotc_types.h>
#include <iotc_error.h>
#include "../../common/src/commandline.h"


/**
 * @macro  Private key filename and maximum key length set.
 */

#define DEFAULT_PRIVATE_KEY_FIILENAME "ec_private.pem"
#define PRIVATE_KEY_BUFFER_SIZE 256

/**
 * @macro  Quality of service, Topic and message are set.
 */
#define QoS 2
#define Topic "Channel"
#define Message "Message"

/**
 * @macro  Incase of warnings
 */
#define UNUSED(x) (void)(x)

/* Application variables. */
iotc_crypto_key_data_t iotc_connect_private_key_data;
char ec_private_key_pem[PRIVATE_KEY_BUFFER_SIZE] = {0};
iotc_context_handle_t iotc_context = IOTC_INVALID_CONTEXT_HANDLE;
static iotc_timed_task_handle_t delayed_publish_task = IOTC_INVALID_TIMED_TASK_HANDLE;


/**
 * @brief: iotc_command_line() - Parse arguments for the options specified in the options string and check for missing parameters.
 * Implements a command line argument parser, defined in commandline.c
 */
int iotc_command_line(int argc, char* argv[]) {
  char options[] = "h:p:d:t:m:f:";
  int missing_parameter = 0;
  int ret_val = 0;

  printf("\n%s\n%s\n", argv[0], iotc_cilent_version_str);// Logging the executable name (argv[0]) and library version (iotc_cilent_version_str)


  ret_val = iotc_parse(argc, argv, options, sizeof(options)); //Parse arguments
 
  //If an error is returned, function exits here
  if (-1 == ret_val) {
    return -1;
  }

  //Check for missing parameters in command line
  if (NULL == iotc_project_id) {
    missing_parameter = 1;
    printf("-p --project_id is required\n");
  }

  if (NULL == iotc_device_path) {
    missing_parameter = 1;
    printf("-d --device_path is required\n");
  }

  if (NULL == iotc_publish_topic) {
    missing_parameter = 1;
    printf("-t --publish_topic is required\n");
  }

  if (1 == missing_parameter) {
    printf("\n");//Exiting
    return -1;
  }

  return 0;
}

/**
 * @brief: load_key() - Function to load key.
 */
int load_key(char* buf_ec_private_key_pem, size_t buf_len) {
  FILE* file1 = fopen(iotc_private_key_filename, "rb");
  if (file1 == NULL) {
    printf("ERROR!\n");
    printf(
        "\tMissing Private Key required for JWT signing.\n"
        "\tPlease copy and paste your device's EC private key into\n"
        "\ta file with the following path based on this executable's\n"
        "\tcurrent working dir:\n\t\t\'%s\'\n\n"
        "\tAlternatively use the --help command line parameter to learn\n"
        "\thow to set a path to your file using command line arguments\n",
        iotc_private_key_filename);
    return -1;
  }

  fseek(file1, 0, SEEK_END);
  long file_size = ftell(file1);
  rewind(file1);

  if ((size_t)file_size > buf_len) {
    printf("private key file size of %lu bytes is larger that certificate buffer " "size of %lu bytes\n", file_size, (long)buf_len);
    fclose(file1);
    return -1;
  }

  long bytes_read = fread(buf_ec_private_key_pem, 1, file_size, file1);
  fclose(file1);

  if (bytes_read != file_size) {
    printf("could not fully read private key file\n");
    return -1;
  }

  return 0;
}

/**
 * @brief: rec_message() - Function to print received message on the clients's subscribed topic. 
 * This is part of subsciption callback.
 */
void rec_message(iotc_context_handle_t in_context_handle, iotc_sub_call_type_t call_type, const iotc_sub_call_params_t* const params, iotc_state_t state, void* user_data )
{
    UNUSED(in_context_handle) ;
    UNUSED(state) ;
    UNUSED(user_data) ;
    if (call_type == IOTC_SUB_CALL_MESSAGE)
    {
      // const char *topic = params->topic ;
      printf("Received message %s on topic %s\n", params->message.temporary_payload_data, params->message.topic) ;
    }
}


/**
 * @brief: on_connection_state_changed() - Check the connection state and perform the required tasks such as 
 * handling of such as successful and unsuccessful connection.
 */
void on_connection_state_changed(iotc_context_handle_t in_context_handle, void *data, iotc_state_t state)
{
      iotc_connection_data_t *connection_data = (iotc_connection_data_t *)data;

      switch (connection_data->connection_state) {
      case IOTC_CONNECTION_STATE_OPENED: printf("connected!\n");
                                        break;

      case IOTC_CONNECTION_STATE_OPEN_FAILED: printf("ERROR!\tConnection has failed reason %d\n\n", state);
        // Exit it out of the application by stopping the event loop.
                                              iotc_events_stop();//Exit event handler
                                              break;

      case IOTC_CONNECTION_STATE_CLOSED: if (IOTC_INVALID_TIMED_TASK_HANDLE != delayed_publish_task) {
                                         iotc_cancel_timed_task(delayed_publish_task);
                                         delayed_publish_task = IOTC_INVALID_TIMED_TASK_HANDLE;
                                        }

                                        if (state == IOTC_STATE_OK) {
                                            iotc_events_stop();//Exit event handler
                                        } else {
                                            printf("connection closed - reason %d!\n", state);
                                            iotc_connect(in_context_handle, connection_data->username, connection_data->password, connection_data->client_id, connection_data->connection_timeout, connection_data->keepalive_timeout, &on_connection_state_changed);
                                        }
                                        break;
      default:  printf("wrong value\n");
                break;
      }
}


/**
 * @brief: publish() - Function to publish message stored in global opts struture which is passed in as payload to Cloud IOT Core.
 */
void publish(iotc_context_handle_t context_handle,iotc_timed_task_handle_t timed_task, void* user_data)
 {
 
  UNUSED(timed_task);//timed_task is a handle to the reoccurring task that may have invoked this function.
  UNUSED(user_data); //user data is a anonymous payload that the application can use to feed non-typed information to scheduled function calls / events.

  printf("Publishing msg \"%s\" to topic: \"%s\"\n", Message, Topic);

  iotc_publish(context_handle, Topic, Message, IOTC_MQTT_QOS_AT_LEAST_ONCE, /*callback=*/NULL, /*user_data=*/NULL);
}

/**
 * @brief: pass_key() - This example assumes the private key to be used to  sign the IoT Core. 
 * Connect JWT credential is a PEM encoded ES256 private key, and passes it IoT Core Device Client functions as a byte array.   
 *
 */
int pass_key(int argc, char* argv[])
{
    if (0 != iotc_command_line(argc, argv)) {
    return -1;
  }

  if (0 != load_key(ec_private_key_pem, PRIVATE_KEY_BUFFER_SIZE))//load the key from disk into memory
  {
    printf("\nError loading IoT Core private key\n\n");
    return -1;
  }

  /*Key type descriptors. Format the key type descriptors so the client understands
  which type of key is being reprenseted. In this case, a PEM encoded
  byte array of a ES256 key*/
  iotc_connect_private_key_data.crypto_key_signature_algorithm = IOTC_CRYPTO_KEY_SIGNATURE_ALGORITHM_ES256;
  iotc_connect_private_key_data.crypto_key_union_type = IOTC_CRYPTO_KEY_UNION_TYPE_PEM;
  iotc_connect_private_key_data.crypto_key_union.key_pem.key = ec_private_key_pem;

  const iotc_state_t error_init = iotc_initialize();// Initialize iotc library

  if (IOTC_STATE_OK != error_init)
  {
    printf(" Failed to initialize, error: %d\n", error_init);
    return -1;
  }

/*A context created to represent a Connection on a single socket, and can be 
  used to publish and subscribe to numerous topics.*/
  iotc_context = iotc_create_context();
  if (IOTC_INVALID_CONTEXT_HANDLE >= iotc_context)
  {
    printf("Failed to create context, error: %d\n", -iotc_context);
    return -1;
  }


  const uint16_t connection_timeout = 10;
  const uint16_t keepalive_timeout = 20;

  //Generate the client authentication JWT
  char jwt[IOTC_JWT_SIZE] = {0};
  size_t bytes_written = 0;
  iotc_state_t state = iotc_create_iotcore_jwt(iotc_project_id, /*jwt_expiration_period_sec=*/3600, &iotc_connect_private_key_data, jwt, IOTC_JWT_SIZE, &bytes_written);

  if (IOTC_STATE_OK != state)
  {
    printf("iotc_create_iotcore_jwt returned with error: %ul : %s\n", state,
           iotc_get_state_string(state));
    return -1;
  }

  return 0;
}

/**
 * @brief: main() -Main function
 */
int main(int argc, char* argv[])
{
  
  int ret=check(argc,argv);
  if(ret == 0){
    iotc_connect(iotc_context, /*username=*/NULL, /*password=*/jwt,/*client_id=*/iotc_device_path, connection_timeout, keepalive_timeout, &on_connection_state_changed);

    iotc_subscribe(iotc_context, Topic, Qos, /*(rec_message) callback*/&rec_message, /*user_data*/NULL) ;
    publish(iotc_context, IOTC_INVALID_TIMED_TASK_HANDLE,/*user_data=*/NULL);

    iotc_events_process_blocking();//to process connection requests, and for the client to regularly check the sockets for incoming data

  /*  Cleanup the default context, releasing its memory */
    iotc_delete_context(iotc_context);

  /* Cleanup internal allocations that were created by iotc_initialize. */
    iotc_shutdown();//Closing the connection
    return 0;
  }
  return -1;
}
