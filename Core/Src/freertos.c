/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <builtin_interfaces/msg/time.h>
#include <geometry_msgs/msg/transform_stamped.h>
#include <nav_msgs/msg/odometry.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rcutils/allocator.h>
#include <rmw_microros/rmw_microros.h>
#include <rosidl_runtime_c/string_functions.h>
#include <std_msgs/msg/int32.h>
#include <tf2_msgs/msg/tf_message.h>
#include <uxr/client/transport.h>

#include "usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ODOM_FRAME_ID                 "odom"
#define BASE_FOOTPRINT_FRAME_ID       "base_footprint"
#define ODOM_TIMER_PERIOD_MS          50U
#define ODOM_LINEAR_X_MPS             0.1
#define PING_TIMER_PERIOD_MS          1000U
#define RCL_RETRY_DELAY_MS            500U
#define APP_ERROR_DELAY_MS            1000U
#define EXECUTOR_SPIN_TIMEOUT_MS      10U
#define EXECUTOR_LOOP_DELAY_MS        10U
#define APP_MILLISECONDS_PER_SECOND   1000.0
#define APP_NANOSECONDS_PER_SECOND    1000000000LL

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define RCL_RETRY_UNTIL_OK(call)          \
  do                                      \
  {                                       \
    while ((call) != RCL_RET_OK)          \
    {                                     \
      retry_after_rcl_error();            \
    }                                     \
  } while (0)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static rcl_publisher_t odom_publisher;
static rcl_publisher_t tf_publisher;
static rcl_timer_t odom_timer;
static nav_msgs__msg__Odometry odom_msg;
static tf2_msgs__msg__TFMessage tf_msg;
static double simulated_x_m = 0.0;
static uint32_t odom_last_tick = 0U;

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
uint32_t defaultTaskBuffer[ 3000 ];
osStaticThreadDef_t defaultTaskControlBlock;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .cb_mem = &defaultTaskControlBlock,
  .cb_size = sizeof(defaultTaskControlBlock),
  .stack_mem = &defaultTaskBuffer[0],
  .stack_size = sizeof(defaultTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
bool cubemx_transport_open(struct uxrCustomTransport * transport);
bool cubemx_transport_close(struct uxrCustomTransport * transport);

size_t cubemx_transport_write(
    struct uxrCustomTransport * transport,
    const uint8_t * buf,
    size_t len,
    uint8_t * err);

size_t cubemx_transport_read(
    struct uxrCustomTransport * transport,
    uint8_t * buf,
    size_t len,
    int timeout,
    uint8_t * err);

void * microros_allocate(size_t size, void * state);
void microros_deallocate(void * pointer, void * state);
void * microros_reallocate(void * pointer, size_t size, void * state);
void * microros_zero_allocate(size_t number_of_elements, size_t size_of_element, void * state);

static bool init_odom_tf_messages(void);
static builtin_interfaces__msg__Time get_micro_ros_time(void);
static void odom_timer_callback(rcl_timer_t * timer, int64_t last_call_time);
static void stop_task_forever(void);
static void retry_after_rcl_error(void);
static bool publish_message(rcl_publisher_t * publisher, const void * message);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  (void) argument;

  rmw_uros_set_custom_transport(
      true,
      (void *) &huart1,
      cubemx_transport_open,
      cubemx_transport_close,
      cubemx_transport_write,
      cubemx_transport_read);

  rcl_allocator_t freeRTOS_allocator = rcutils_get_zero_initialized_allocator();

  freeRTOS_allocator.allocate = microros_allocate;
  freeRTOS_allocator.deallocate = microros_deallocate;
  freeRTOS_allocator.reallocate = microros_reallocate;
  freeRTOS_allocator.zero_allocate = microros_zero_allocate;

  if (!rcutils_set_default_allocator(&freeRTOS_allocator))
  {
    stop_task_forever();
  }

  rcl_allocator_t allocator = rcl_get_default_allocator();

  static rclc_support_t support;
  static rcl_node_t node;
  static rcl_publisher_t publisher;
  static rclc_executor_t executor;
  static std_msgs__msg__Int32 msg;

  RCL_RETRY_UNTIL_OK(rclc_support_init(&support, 0, NULL, &allocator));
  RCL_RETRY_UNTIL_OK(rclc_node_init_default(&node, "stm32_f407_node", "", &support));

  RCL_RETRY_UNTIL_OK(rclc_publisher_init_default(
      &publisher,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
      "/stm32_ping"));

  RCL_RETRY_UNTIL_OK(rclc_publisher_init_default(
      &odom_publisher,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry),
      "/odom"));

  RCL_RETRY_UNTIL_OK(rclc_publisher_init_default(
      &tf_publisher,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(tf2_msgs, msg, TFMessage),
      "/tf"));

  if (!init_odom_tf_messages())
  {
    stop_task_forever();
  }

  (void) rmw_uros_sync_session(1000);

  RCL_RETRY_UNTIL_OK(rclc_timer_init_default(
      &odom_timer,
      &support,
      RCL_MS_TO_NS(ODOM_TIMER_PERIOD_MS),
      odom_timer_callback));

  RCL_RETRY_UNTIL_OK(rclc_executor_init(&executor, &support.context, 1, &allocator));

  msg.data = 0;

  RCL_RETRY_UNTIL_OK(rclc_executor_add_timer(&executor, &odom_timer));

  uint32_t last_publish = osKernelGetTickCount();

  for (;;)
  {
    uint32_t now = osKernelGetTickCount();

    if ((now - last_publish) >= pdMS_TO_TICKS(PING_TIMER_PERIOD_MS))
    {
      last_publish = now;

      if (publish_message(&publisher, &msg))
      {
        msg.data++;
      }
    }

    rcl_ret_t spin_ret = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(EXECUTOR_SPIN_TIMEOUT_MS));

    if ((spin_ret != RCL_RET_OK) && (spin_ret != RCL_RET_TIMEOUT))
    {
      rcl_reset_error();
    }

    osDelay(EXECUTOR_LOOP_DELAY_MS);
  }

  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

static bool init_odom_tf_messages(void)
{
  memset(&odom_msg, 0, sizeof(odom_msg));
  memset(&tf_msg, 0, sizeof(tf_msg));

  if (!nav_msgs__msg__Odometry__init(&odom_msg))
  {
    return false;
  }

  if (!geometry_msgs__msg__TransformStamped__Sequence__init(&tf_msg.transforms, 1))
  {
    nav_msgs__msg__Odometry__fini(&odom_msg);
    return false;
  }

  geometry_msgs__msg__TransformStamped * tf = &tf_msg.transforms.data[0];

  if (!rosidl_runtime_c__String__assign(&odom_msg.header.frame_id, ODOM_FRAME_ID) ||
      !rosidl_runtime_c__String__assign(&odom_msg.child_frame_id, BASE_FOOTPRINT_FRAME_ID) ||
      !rosidl_runtime_c__String__assign(&tf->header.frame_id, ODOM_FRAME_ID) ||
      !rosidl_runtime_c__String__assign(&tf->child_frame_id, BASE_FOOTPRINT_FRAME_ID))
  {
    geometry_msgs__msg__TransformStamped__Sequence__fini(&tf_msg.transforms);
    nav_msgs__msg__Odometry__fini(&odom_msg);
    return false;
  }

  odom_msg.pose.pose.orientation.w = 1.0;
  odom_msg.twist.twist.linear.x = ODOM_LINEAR_X_MPS;
  odom_msg.twist.twist.linear.y = 0.0;
  odom_msg.twist.twist.linear.z = 0.0;
  odom_msg.twist.twist.angular.x = 0.0;
  odom_msg.twist.twist.angular.y = 0.0;
  odom_msg.twist.twist.angular.z = 0.0;

  tf->transform.translation.y = 0.0;
  tf->transform.translation.z = 0.0;
  tf->transform.rotation.x = 0.0;
  tf->transform.rotation.y = 0.0;
  tf->transform.rotation.z = 0.0;
  tf->transform.rotation.w = 1.0;

  simulated_x_m = 0.0;
  odom_last_tick = 0U;

  return true;
}

static builtin_interfaces__msg__Time get_micro_ros_time(void)
{
  builtin_interfaces__msg__Time stamp = {0};
  int64_t now_ns = 0;

  if (rmw_uros_epoch_synchronized())
  {
    now_ns = rmw_uros_epoch_nanos();
  }
  else
  {
    now_ns = ((int64_t) osKernelGetTickCount() * APP_NANOSECONDS_PER_SECOND) / configTICK_RATE_HZ;
  }

  if (now_ns < 0)
  {
    now_ns = 0;
  }

  stamp.sec = (int32_t) (now_ns / APP_NANOSECONDS_PER_SECOND);
  stamp.nanosec = (uint32_t) (now_ns % APP_NANOSECONDS_PER_SECOND);

  return stamp;
}

static void odom_timer_callback(rcl_timer_t * timer, int64_t last_call_time)
{
  (void) last_call_time;

  if (timer == NULL)
  {
    return;
  }

  uint32_t now_tick = osKernelGetTickCount();
  double dt = (double) ODOM_TIMER_PERIOD_MS / APP_MILLISECONDS_PER_SECOND;

  if (odom_last_tick != 0U)
  {
    dt = (double) (now_tick - odom_last_tick) / (double) configTICK_RATE_HZ;
  }

  odom_last_tick = now_tick;
  simulated_x_m += ODOM_LINEAR_X_MPS * dt;

  builtin_interfaces__msg__Time stamp = get_micro_ros_time();

  odom_msg.header.stamp = stamp;
  odom_msg.pose.pose.position.x = simulated_x_m;

  geometry_msgs__msg__TransformStamped * tf = &tf_msg.transforms.data[0];
  tf->header.stamp = stamp;
  tf->transform.translation.x = simulated_x_m;

  (void) publish_message(&odom_publisher, &odom_msg);
  (void) publish_message(&tf_publisher, &tf_msg);
}

static void stop_task_forever(void)
{
  for (;;)
  {
    osDelay(APP_ERROR_DELAY_MS);
  }
}

static void retry_after_rcl_error(void)
{
  rcl_reset_error();
  osDelay(RCL_RETRY_DELAY_MS);
}

static bool publish_message(rcl_publisher_t * publisher, const void * message)
{
  if (rcl_publish(publisher, message, NULL) == RCL_RET_OK)
  {
    return true;
  }

  rcl_reset_error();
  return false;
}

/* USER CODE END Application */
