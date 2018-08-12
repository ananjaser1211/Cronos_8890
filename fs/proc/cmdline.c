#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/setup.h>

static char updated_command_line[COMMAND_LINE_SIZE];

static int cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", updated_command_line);
	return 0;
}

static int cmdline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmdline_proc_show, NULL);
}

static const struct file_operations cmdline_proc_fops = {
	.open		= cmdline_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void proc_cmdline_set(char *name, char *value)
{
	char flag_str[COMMAND_LINE_SIZE];
	char *flag_substr;
	char *flag_space_substr;
 	scnprintf(flag_str, COMMAND_LINE_SIZE, "%s=", name);
	flag_substr = strstr(updated_command_line, flag_str);
 	if (flag_substr) {
		flag_space_substr = strchr(flag_substr, ' ');
		scnprintf(updated_command_line, COMMAND_LINE_SIZE, "%.*s%s", (int)(flag_substr - updated_command_line), updated_command_line, flag_space_substr + 1);
	}
 	// flag was not found, insert it
	scnprintf(updated_command_line, COMMAND_LINE_SIZE, "%s %s=%s", updated_command_line, name, value);
}

static int __init proc_cmdline_init(void)
{
		// copy it only once
	strcpy(updated_command_line, saved_command_line);
 	proc_cmdline_set("androidboot.verifiedbootstate", "green");
	proc_cmdline_set("androidboot.warranty_bit", "0");
	proc_cmdline_set("androidboot.fmp_config", "1");

	proc_create("cmdline", 0, NULL, &cmdline_proc_fops);
	return 0;
}
fs_initcall(proc_cmdline_init);
