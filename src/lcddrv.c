#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/i2c.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Bradshaw");
MODULE_DESCRIPTION("Character driver for 2 line LCD display");
MODULE_VERSION("0.01");

#define LCDDRV_ROWS    (4)
#define LCDDRV_COLUMNS (20)

#define LCDDRV_FILE "lcd"
#define LCDDRV_SLAVE_ADDRESS (0x3f)

static char g_fb[LCDDRV_ROWS*LCDDRV_COLUMNS] = "Hello";
static char *g_ptr;
static int g_major;
static int g_devcount;
static struct i2c_client* g_client;

/* This table describes the slave devices we support */
static struct i2c_device_id slave_idtable[] = {
  { "lcd", LCDDRV_SLAVE_ADDRESS },
  { }
};

MODULE_DEVICE_TABLE(i2c,slave_idtable);

/* Forward declaration of file interface functions */
static ssize_t lcddrv_device_read(struct file *, char *, size_t, loff_t *);
static ssize_t lcddrv_device_write(struct file *, const char*, size_t, loff_t *);
static int lcddrv_device_open(struct inode *, struct file *);
static int lcddrv_device_release(struct inode *, struct file *);

/* Forward declaration of driver functions */
static int lcddrv_probe(struct i2c_client*,const struct i2c_device_id*);
static int lcddrv_remove(struct i2c_client*);

/*
static lcddrv_shutdown
static lcddrv_device_write
*/

/* main i2c_driver structure */
struct i2c_driver lcd_driver = {
  .driver = {
    .name = "lcddrv",
  },

  .id_table                     = slave_idtable,
  .probe                        = lcddrv_probe,
  .remove                       = lcddrv_remove,
  /*.shutdown                     = lcddrv_shutdown,*/
};

/* This structure points to all of the device functions */
static struct file_operations file_ops = {
  .read = lcddrv_device_read,
  .write = lcddrv_device_write,
  .open = lcddrv_device_open,
  .release = lcddrv_device_release
};

static int i2c_write(struct i2c_client *client, unsigned char data)
{
  unsigned char buffer[3];
  buffer[0] = data+4+8;
  buffer[1] = data+8;
  buffer[2] = data+4+8;
  i2c_master_send(client,buffer,3);
  return 0;
}

static void lcddrv_init_screen(struct i2c_client *handle)
{
  i2c_write(handle,48);
  i2c_write(handle,48);
  i2c_write(handle,48);
  i2c_write(handle,32);
  i2c_write(handle,32);
  i2c_write(handle,192);
  i2c_write(handle,0);
  i2c_write(handle,128);
  i2c_write(handle,0);
  i2c_write(handle,16);
  i2c_write(handle,0);
  i2c_write(handle,96);
  i2c_write(handle,0);
  i2c_write(handle,224);
  i2c_write(handle,0);
  i2c_write(handle,96);

}

static void lcddrv_put_char(struct i2c_client *client, unsigned char b)
{
  i2c_write(client,(b&0xF0) +1);
  i2c_write(client,((b&0xF)<<4)+1);
}

static void lcddrv_put_cmd(struct i2c_client *client, unsigned char b)
{
  i2c_write(client,(b&0xF0));
  i2c_write(client,((b&0xF)<<4));
}

static void lcddrv_reset_display(struct i2c_client *client)
{
  lcddrv_put_cmd(client,1);
  msleep(2);
}

/* When a process reads from our device, this gets called. */
static ssize_t lcddrv_device_read(struct file *filp, char *buffer, size_t len, loff_t *offset)
{
  int bytes_read = 0;

  /* If we’re at the end, loop back to the beginning */
  if (*g_ptr == 0) {
    g_ptr = g_fb;
    return 0;
  }
  /* Put data in the buffer */
  while (len && *g_ptr) {
    /* Buffer is in user data, not kernel, so you can’t just reference
     * with a pointer. The function put_user handles this for us */
    put_user(*(g_ptr++), buffer++);
    len--;
    bytes_read++;
  }
  return bytes_read;
}

/* When a process writes to  our device, this gets called. */
static ssize_t lcddrv_device_write(struct file *filp, const char *buffer,
				   size_t len, loff_t *offset)
{
  int idx=0;

  /* Copy message into kernel space framebuffer */
  memset(g_fb,0,len+1);
  copy_from_user(g_fb,buffer,len);

  /* Reset the display */
  lcddrv_reset_display(g_client);

  /* Write new message */
  while (idx<len)
    lcddrv_put_char(g_client,g_fb[idx++]);

  return len;
}

static int lcddrv_device_open(struct inode *inode, struct file *file)
{
  if (g_devcount)
    {
      return -EBUSY;
    }
  g_devcount++;
  try_module_get(THIS_MODULE);
  return 0;
}

static int lcddrv_device_release(struct inode *inode, struct file *file)
{
  g_devcount--;
  module_put(THIS_MODULE);
  return 0;
}

static int lcddrv_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
  char buf[1];
  printk(KERN_INFO "lcddrv_probe\n");

  /* Store the client */
  g_client = client;
  
  /* Turn on the backlight */
  buf[0]=12;
  i2c_master_send(client,buf,1);

  /* Initialize the screen */
  lcddrv_init_screen(client);
  
  return 0;
}

static int lcddrv_remove(struct i2c_client *client)
{
  char buf[1];
  printk(KERN_INFO "lcddrv_remove\n");

  /* Turn off the backlight */
  buf[0]=4;
  i2c_master_send(client,buf,1);

  return 0;
}

static int __init lcddrv_init(void)
{
  int retVal;
    
  printk(KERN_INFO "lcddrv_init()\n");
  g_ptr = g_fb;
  g_major = register_chrdev(0, LCDDRV_FILE, &file_ops);
  g_devcount=0;
  printk(KERN_INFO "lkm_example module loaded with device major number %d\n", g_major);

  if ( (retVal = i2c_add_driver(&lcd_driver) ) >= 0)
    printk(KERN_INFO "Added lcddrv driver\n");
  else
    printk(KERN_INFO "Error adding lcddrv driver: %d\n", retVal);

  return retVal;
}

static void __exit lcddrv_exit(void)
{
  i2c_del_driver(&lcd_driver);
  printk(KERN_INFO "Removed lcddrv driver\n");
  unregister_chrdev(g_major, LCDDRV_FILE );
  printk(KERN_INFO "Removed lcddrv fs interface\n");
}

module_init(lcddrv_init);
module_exit(lcddrv_exit);
