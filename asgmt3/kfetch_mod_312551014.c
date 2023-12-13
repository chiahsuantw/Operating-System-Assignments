#include <linux/cdev.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/utsname.h>

#define KFETCH_RELEASE (1 << 0)
#define KFETCH_NUM_CPUS (1 << 1)
#define KFETCH_CPU_MODEL (1 << 2)
#define KFETCH_MEM (1 << 3)
#define KFETCH_UPTIME (1 << 4)
#define KFETCH_NUM_PROCS (1 << 5)

#define DEVICE_NAME "kfetch"
#define BUF_SIZE 1024

struct kfetch_dev {
  dev_t id;
  struct cdev cdev;
  struct class *class;
  struct device *device;
} kfetch;

static int info_mask = -1;

// A lock to prevent multiple access to the device
enum {
  DEV_NOT_USED = 0,
  DEV_EXCLUSIVE_OPEN = 1,
};
static atomic_t device_opened = ATOMIC_INIT(DEV_NOT_USED);

static int kfetch_open(struct inode *inode, struct file *file) {
  if (atomic_cmpxchg(&device_opened, DEV_NOT_USED, DEV_EXCLUSIVE_OPEN))
    return -EBUSY;
  try_module_get(THIS_MODULE); // Increase the usage count
  return 0;
}

static int kfetch_release(struct inode *inode, struct file *file) {
  atomic_set(&device_opened, DEV_NOT_USED);
  module_put(THIS_MODULE); // Decrease the usage count
  return 0;
}

static ssize_t kfetch_read(struct file *file, char __user *buffer,
                           size_t length, loff_t *offset) {
  // Fetch and format information
  struct sys_info {
    char *hostname;
    char *divider;
    char kernel[64];
    char cpu[64];
    char cpus[64];
    char mem[64];
    char procs[64];
    char uptime[64];
  } info;

  // Hostname
  info.hostname = utsname()->nodename;

  // Divider
  info.divider = kcalloc(strlen(info.hostname) + 1, sizeof(char), GFP_KERNEL);
  memset(info.divider, '-', strlen(info.hostname));

  // Kernel release name
  snprintf(info.kernel, 64, "Kernel:   %s", utsname()->release);

  // CPU model name
  struct cpuinfo_x86 *c = &cpu_data(0);
  snprintf(info.cpu, 64, "CPU:      %s", c->x86_model_id);

  // Number of online cores
  snprintf(info.cpus, 64, "CPUs:     %d / %d", num_online_cpus(),
           num_present_cpus());

  // Memory used
  struct sysinfo mem_info;
  si_meminfo(&mem_info);
  snprintf(info.mem, 64, "Mem:      %ld MB / %ld MB",
           mem_info.freeram * mem_info.mem_unit / (1024 * 1024),
           mem_info.totalram * mem_info.mem_unit / (1024 * 1024));

  // Number of processes
  struct task_struct *task;
  int proc_cnt = 0;
  for_each_process(task) { proc_cnt++; }
  snprintf(info.procs, 64, "Procs:    %d", proc_cnt);

  // Uptime
  snprintf(info.uptime, 64, "Uptime:   %lld mins",
           ktime_divns(ktime_get_coarse_boottime(), NSEC_PER_SEC) / 60);

  char logo[8][20] = {0};
  strcpy(logo[0], "                   ");
  strcpy(logo[1], "        .-.        ");
  strcpy(logo[2], "       (.. |       ");
  strcpy(logo[3], "       <>  |       ");
  strcpy(logo[4], "      / --- \\      ");
  strcpy(logo[5], "     ( |   | |     ");
  strcpy(logo[6], "   |\\\\_)___/\\)/\\   ");
  strcpy(logo[7], "  <__)------(__/   ");

  char info_buf[8][64] = {0};
  int info_cnt = 0;
  strcpy(info_buf[info_cnt++], info.hostname);
  strcpy(info_buf[info_cnt++], info.divider);
  if (info_mask & KFETCH_RELEASE)
    strcpy(info_buf[info_cnt++], info.kernel);
  if (info_mask & KFETCH_CPU_MODEL)
    strcpy(info_buf[info_cnt++], info.cpu);
  if (info_mask & KFETCH_NUM_CPUS)
    strcpy(info_buf[info_cnt++], info.cpus);
  if (info_mask & KFETCH_MEM)
    strcpy(info_buf[info_cnt++], info.mem);
  if (info_mask & KFETCH_NUM_PROCS)
    strcpy(info_buf[info_cnt++], info.procs);
  if (info_mask & KFETCH_UPTIME)
    strcpy(info_buf[info_cnt++], info.uptime);

  // Concat output
  char kfetch_buf[BUF_SIZE] = {0};
  for (int i = 0; i < 8; i++) {
    if (i != 0)
      strcat(kfetch_buf, "\n");
    strcat(kfetch_buf, logo[i]);
    strcat(kfetch_buf, info_buf[i]);
  }

  int bytes_read = length > strlen(kfetch_buf) ? strlen(kfetch_buf) : length;
  if (copy_to_user(buffer, kfetch_buf, bytes_read)) {
    pr_alert("Failed to copy data to user");
    return -1;
  }
  return bytes_read;
}

static ssize_t kfetch_write(struct file *file, const char __user *buffer,
                            size_t length, loff_t *offset) {
  // Set the information mask
  if (copy_from_user(&info_mask, buffer, length)) {
    pr_alert("Failed to copy data from user");
    return -1;
  }
  return 0;
}

static struct file_operations kfetch_ops = {.owner = THIS_MODULE,
                                            .open = kfetch_open,
                                            .release = kfetch_release,
                                            .read = kfetch_read,
                                            .write = kfetch_write};

static int __init kfetch_init(void) {
  // Register the character device
  alloc_chrdev_region(&kfetch.id, 0, 1, DEVICE_NAME);
  cdev_init(&kfetch.cdev, &kfetch_ops);
  kfetch.cdev.owner = THIS_MODULE;
  cdev_add(&kfetch.cdev, kfetch.id, 1);
  kfetch.class = class_create(THIS_MODULE, DEVICE_NAME);
  kfetch.device =
      device_create(kfetch.class, NULL, kfetch.id, NULL, DEVICE_NAME);
  return 0;
}

static void __exit kfetch_exit(void) {
  // Unregister the character device
  device_destroy(kfetch.class, kfetch.id);
  class_destroy(kfetch.class);
  cdev_del(&kfetch.cdev);
  unregister_chrdev_region(kfetch.id, 1);
}

module_init(kfetch_init);
module_exit(kfetch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chia-Hsuan Lin");
MODULE_DESCRIPTION("A character device driver");
