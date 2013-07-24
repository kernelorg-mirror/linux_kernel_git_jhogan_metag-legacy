
#include <linux/module.h>
#include <linux/uaccess.h>

#ifdef CONFIG_SOC_CHORUS2
extern unsigned long __replay_text_start;
extern unsigned long __replay_text_end;
#endif

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;
	unsigned long pc = instruction_pointer(regs);

#ifdef CONFIG_SOC_CHORUS2
	/*
	 * If we hit an exception in the replay code then we have to find
	 * the original PC to find out where the appropriate fixup is.
	 */
	if (current_thread_info()->replay_regs &&
	    pc >= (unsigned long)&__replay_text_start &&
	    pc < (unsigned long)&__replay_text_end) {
		memcpy(regs, current_thread_info()->replay_regs,
		       sizeof(*regs));
		pc = instruction_pointer(regs);
	}
#endif
	fixup = search_exception_tables(pc);
	if (fixup)
		regs->ctx.CurrPC = fixup->fixup;

	return fixup != NULL;
}
