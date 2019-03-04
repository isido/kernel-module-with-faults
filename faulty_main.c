/*
 * Faulty: A kernel module with intentional (and unintentional?) bugs
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/slab.h>

#define BUF_SIZE 256

struct dentry *dir;
const char *root = "ffaulty";

static int init_endpoint(struct dentry *dir, const char *fn, const struct file_operations *fops);
static ssize_t sbo_read(struct file *fps, char *buf, size_t len, loff_t *offset);
static ssize_t sbo_write(struct file *fps, const char *buf, size_t len, loff_t *offset);
static ssize_t slab_read(struct file *fps, char *buf, size_t len, loff_t *offset);
static ssize_t slab_write(struct file *fps, const char *buf, size_t len, loff_t *offset);
static void slab_operate_with_other_data(void);
static ssize_t unsigned_overflow_read(struct file *fps, char *buf, size_t len, loff_t *offset);
static ssize_t signed_underflow_read(struct file *fps, char *buf, size_t len, loff_t *offset);
static ssize_t format_read(struct file *fps, char *buf, size_t len, loff_t *offset);
static ssize_t format_write(struct file *fps, const char *buf, size_t len, loff_t *offset);
static void non_reachable_function(void);

// stack buffer overflow
static char *buffer = "just some small data buffer\n";

static const struct file_operations fops_sbo = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = sbo_read,
	.write = sbo_write,
};

// slab corruption
static const struct file_operations fops_slab = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = slab_read,
	.write = slab_write,
};

struct some_data {
	char data[10];
	bool flag_which_is_never_set;
};

struct some_data *user_controlled;
struct some_data *other_data;
bool toggle;

// under/overflow
static u8 unsigned_counter = 250;
static s8 signed_counter = -124;

static const struct file_operations fops_overflow = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = unsigned_overflow_read,
};

static const struct file_operations fops_underflow = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = signed_underflow_read,
};

// format string bug
static char *some_string = "A write to this endpoint will get copied to kernel message buffer\n";

static const struct file_operations fops_format = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = format_read,
	.write = format_write,
};

static int __init mod_init(void)
{
	pr_debug("Faulty: creating debugfs-endpoints\n");

	dir = debugfs_create_dir(root, NULL);

	if (dir == ERR_PTR(-ENODEV)) {
		pr_err
		    ("Faulty: Debugfs doesn't seem to be compiled into the kernel\n");
		return -ENODEV;
	}

	if (dir == NULL) {
		pr_err
		    ("Faulty: Cannot create debugfs-entry '%s'", root);
		return -ENOENT;
	}

	if (!init_endpoint(dir, "sbo", &fops_sbo))
		pr_debug
		    ("Faulty: Stack buffer overflow at debugfs '%s/sbo'\n", root);
	else
		pr_err
		    ("Faulty: Cannot create debugfs-entry %s/sbo\n", root);

	if (!init_endpoint(dir, "slab", &fops_slab))
	    pr_debug("Faulty: Slab buffer overflow at debugfs '%s/slab'\n", root);
	else
	    pr_err("Faulty: Cannot create debugfs-entry %s/slab\n", root);

	if (!init_endpoint(dir, "data-race", NULL)) // TODO implement me
	    pr_debug("Faulty: Data race at debugfs '%s/data-race'\n", root);
	else
	    pr_err("Faulty: Cannot create debugfs-entry %s/data-race\n", root);

	if (!init_endpoint(dir, "overflow", &fops_overflow))
	    pr_debug("Faulty: Unsigned integer overflow at debugfs '%s/overflow'\n", root);
	else
	    pr_err("Faulty: Cannot create debugfs-entry %s/overflow\n", root);

	if (!init_endpoint(dir, "underflow", &fops_underflow))
	    pr_debug("Faulty: Signed integer underflow at debugfs '%s/underflow'\n", root);
	else
	    pr_err("Faulty: Cannot create debugfs-entry %s/underflow\n", root);

	if (!init_endpoint(dir, "format", &fops_format))
	    pr_debug("Faulty: Format string bug at debugfs '%s/format'\n", root);
	else
	    pr_err("Faulty: Cannot create debugfs-entry %s/format\n", root);

	pr_debug("Faulty: module loaded\n");
	return 0;

}

static void __exit mod_exit(void)
{
	debugfs_remove_recursive(dir);
	pr_debug("Faulty: Unloaded faulty kernel module\n");
}

static int init_endpoint(struct dentry *dir, const char *fn, const struct file_operations *fops)
{
	struct dentry *fil = debugfs_create_file(fn, 0644, dir, NULL, fops);

	if (fil == NULL) {
		pr_err("Faulty: Cannot create endpoint %s\n", fn);
		return -ENOENT;
	}

	return 0;
}

static ssize_t sbo_read(struct file *fps, char __user *buf, size_t len,
			loff_t *offset)
{
	return simple_read_from_buffer(buf, len, offset, buffer,
				       strlen(buffer));
}

static ssize_t sbo_write(struct file *fps, const char __user *buf, size_t len,
			 loff_t *offset)
{
	int kbuf_size = 10;
	int flag = 0; // variable to clobber
	char kbuf[kbuf_size];
	int bytes_written = 0;

	// Fault-SBO: length of the incoming data is used instead of
	// target buffer length (kbuf_size)
	bytes_written = simple_write_to_buffer(kbuf, len, offset,
					       buf, len);

	// TODO: another fault here?
	//strncpy(buffer, kbuf, len);

	// we'll bypass stack canary evasion at this time
	if (flag != 0) {
		non_reachable_function();
	}

	return bytes_written;
}


static ssize_t slab_read(struct file *fps, char __user *buf, size_t len,
			loff_t *offset)
{
	slab_operate_with_other_data();

	if (!user_controlled) {
		pr_debug("Faulty: Slab - Read, no data\n");
		return 0;
	}

	pr_info("Faulty: Slab - Read, there is data\n");
	return simple_read_from_buffer(buf, len, offset,
				user_controlled->data, strlen(user_controlled->data));

}

static ssize_t slab_write(struct file *fps, const char __user *buf, size_t len,
			 loff_t *offset)
{
	slab_operate_with_other_data();

	if (!user_controlled) {
		pr_debug("Faulty: Slab - Write, No data\n");
	} else {
		pr_debug("Faulty: Slab - Write, Free old data\n");
		kfree(user_controlled);
	}
	user_controlled = kmalloc(sizeof(struct some_data), GFP_KERNEL);

	// TODO test conditions
	if (other_data->flag_which_is_never_set)
		non_reachable_function();

	return simple_write_to_buffer(user_controlled->data, len, offset,
				buf, len);

}

// TODO: make this double freeable
void slab_operate_with_other_data(void)
{
	if (!toggle) {
		toggle = true;
		pr_debug("Faulty: Slab - allocating other data");
		other_data = kzalloc(sizeof(struct some_data), GFP_KERNEL);
	} else {
		pr_debug("Faulty: Slab - freeing other data");
		kfree(other_data);
		toggle = false;
    }
}

static ssize_t unsigned_overflow_read(struct file *fps, char __user *buf, size_t len,
			loff_t *offset)
{
	char *buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
	ssize_t n = 0;

	snprintf(buffer, BUF_SIZE, "Faulty: Overflow - Counter value :%d\n",
		unsigned_counter++); // note the behaviour of counter

	if (unsigned_counter == 1)
		non_reachable_function();

	n =  simple_read_from_buffer(buf, len, offset, buffer,
				       strlen(buffer));
	kfree(buffer);
	return n;
}

static ssize_t signed_underflow_read(struct file *fps, char __user *buf, size_t len,
			loff_t *offset)
{
	char *buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
	ssize_t n = 0;

	snprintf(buffer, BUF_SIZE, "Faulty: Underflow - Counter value :%d\n",
		signed_counter--); // note the behaviour of counter

	if (signed_counter == 126)
		non_reachable_function();

	n =  simple_read_from_buffer(buf, len, offset, buffer,
				       strlen(buffer));
	kfree(buffer);
	return n;
}

static ssize_t format_read(struct file *fps, char __user *buf, size_t len,
			loff_t *offset)
{
	return simple_read_from_buffer(buf, len, offset, some_string,
				       strlen(some_string));
}

static ssize_t format_write(struct file *fps, const char __user *buf, size_t len,
			 loff_t *offset)
{
	char buffer[BUF_SIZE];
	ssize_t n;

	n = simple_write_to_buffer(&buffer, BUF_SIZE, offset, buf, len);
	buffer[n] = '\0';
	pr_info("Faulty: %s\n", buffer);
	// pr_info(buffer); // this would generate a compile-time error
	printk(buffer); // vulnerable
	return n;
}

static void non_reachable_function(void)
{
	pr_info("Faulty: This function should not be reachable.\n");
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A Kernel Module with Faults");
MODULE_AUTHOR("Ilja Sidoroff");
