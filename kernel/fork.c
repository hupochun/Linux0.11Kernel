/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
/*
 * 'fork.c'�к���ϵͳ����'fork'�ĸ����ӳ��򣨲μ�system_call.s�����Լ�һЩ��������
 * ('verify_area')��һ�����˽���fork���ͻᷢ�����Ƿǳ��򵥵ģ����ڴ����ȴ��Щ�Ѷȡ�
 * �μ�'mm/mm.c'�е�'copy_page_tables()'��
 */
#include <errno.h>				// �����ͷ�ļ�������ϵͳ�и��ֳ����š�(Linus ��minix ��������)��

#include <linux/sched.h>		// ���ȳ���ͷ�ļ�������������ṹtask_struct����ʼ����0 �����ݣ�
								// ����һЩ�й��������������úͻ�ȡ��Ƕ��ʽ��ຯ������䡣
#include <linux/kernel.h>		// �ں�ͷ�ļ�������һЩ�ں˳��ú�����ԭ�ζ��塣
#include <asm/segment.h>		// �β���ͷ�ļ����������йضμĴ���������Ƕ��ʽ��ຯ����
#include <asm/system.h>			// ϵͳͷ�ļ������������û��޸�������/�ж��ŵȵ�Ƕ��ʽ���ꡣ

// дҳ����֤����ҳ�治��д������ҳ�档������ mm/memory.c��
extern void write_verify(unsigned long address);

// ���½��̺ţ���ֵ����get_empty_process()����.
long last_pid=0;

//// ���̿ռ�����дǰ��֤������
// �Ե�ǰ���̵ĵ�ַaddr ��addr+size ��һ�ν��̿ռ���ҳΪ��λִ��д����ǰ�ļ�������
// ��ҳ����ֻ���ģ���ִ�й�������͸���ҳ�������дʱ���ƣ���
void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	// ����ʼ��ַstart ����Ϊ������ҳ����߽翪ʼλ�ã�ͬʱ��Ӧ�ص�����֤�����С��
	// ��ʱstart �ǵ�ǰ���̿ռ��е����Ե�ַ��
	size += start & 0xfff;
	start &= 0xfffff000;
	start += get_base(current->ldt[2]);		// ��ʱstart ���ϵͳ�������Կռ��еĵ�ַλ�á�
	while (size>0) {
		size -= 4096;
		write_verify(start);				// дҳ����֤����ҳ�治��д������ҳ�档��mm/memory.c��
		start += 4096;
	}
}

// ����������Ĵ�������ݶλ�ַ���޳�������ҳ����
// nr Ϊ������ţ�p �����������ݽṹ��ָ�롣
int copy_mem(int nr,struct task_struct * p)
{
	unsigned long old_data_base,new_data_base,data_limit;
	unsigned long old_code_base,new_code_base,code_limit;

	code_limit = get_limit (0x0f);			// ȡ�ֲ����������д�������������ж��޳���
  	data_limit = get_limit (0x17);			// ȡ�ֲ��������������ݶ����������ж��޳���
 	old_code_base = get_base (current->ldt[1]);		// ȡԭ����λ�ַ��
 	old_data_base = get_base (current->ldt[2]);		// ȡԭ���ݶλ�ַ��
  	if (old_data_base != old_code_base)		// 0.11 �治֧�ִ�������ݶη����������
    	panic ("We don't support separate I&D");
  	if (data_limit < code_limit)			// ������ݶγ��� < ����γ���Ҳ���ԡ�
    	panic ("Bad data_limit");
  	new_data_base = new_code_base = nr * 0x4000000;	// �»�ַ=�����*64Mb(�����С)��
  	p->start_code = new_code_base;
  	set_base (p->ldt[1], new_code_base);	// ���ô�����������л�ַ��
  	set_base (p->ldt[2], new_data_base);	// �������ݶ��������л�ַ��
  	if (copy_page_tables (old_data_base, new_data_base, data_limit))
    {				// ���ƴ�������ݶΡ�
      free_page_tables (new_data_base, data_limit);	// ����������ͷ�������ڴ档
      return -ENOMEM;
    }
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
 /*
  * OK����������Ҫ��fork �ӳ���������ϵͳ������Ϣ(task[n])�������ñ�Ҫ�ļĴ�����
  * ���������ظ������ݶΡ�
  */
// ���ƽ��̡�
int copy_process(int nr,long child_ebp,long child_edi,long child_esi,long child_gs,long none,
		long child_ebx,long child_ecx,long child_edx,
		long child_fs,long child_es,long child_ds,
		long child_eip,long child_cs,long child_eflags,long child_esp,long child_ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();	// Ϊ���������ݽṹ�����ڴ档
	if (!p)	return -EAGAIN;				// ����ڴ����������򷵻س����벢�˳���
		
	task[nr] = p;								// ��������ṹָ��������������С�
	// ����nr Ϊ����ţ���ǰ��find_empty_process()���ء�
	*p = *current;	/* NOTE! this doesn't copy the supervisor stack *//* ע�⣡���������Ḵ�Ƴ����û��Ķ�ջ */
	p->state = TASK_UNINTERRUPTIBLE;			// ���½��̵�״̬����Ϊ�����жϵȴ�״̬��
  	p->pid = last_pid;							// �½��̺š���ǰ�����find_empty_process()�õ���
  	p->father = current->pid;					// ���ø����̺š�
 	p->counter = p->priority;
 	p->signal = 0;								// �ź�λͼ��0��
  	p->alarm = 0;
  	p->leader = 0;		/* process leadership doesn't inherit */
						/* ���̵��쵼Ȩ�ǲ��ܼ̳е� */
  	p->utime = p->stime = 0;					// ��ʼ���û�̬ʱ��ͺ���̬ʱ�䡣
  	p->cutime = p->cstime = 0;					// ��ʼ���ӽ����û�̬�ͺ���̬ʱ�䡣
  	p->start_time = jiffies;					// ��ǰ�δ���ʱ�䡣
  	
  	RECORD_TASK_STATE(p->pid, TS_CREATE, jiffies);
			
	// ������������״̬��TSS ��������ݡ�
  	p->tss.back_link = 0;
  	p->tss.esp0 = PAGE_SIZE + (long) p;			// ��ջָ�루�����Ǹ�����ṹp ������1 ҳ
												// ���ڴ棬���Դ�ʱesp0 ����ָ���ҳ���ˣ���
  	p->tss.ss0 = 0x10;							// ��ջ��ѡ������ں����ݶΣ�[??]��
  	p->tss.eip = child_eip;							// ָ�����ָ�롣
  	p->tss.eflags = child_eflags;						// ��־�Ĵ�����
	p->tss.eax = 0;
	p->tss.ecx = child_ecx;
	p->tss.edx = child_edx;
	p->tss.ebx = child_ebx;
	p->tss.esp = child_esp;
	p->tss.ebp = child_ebp;
	p->tss.esi = child_esi;
	p->tss.edi = child_edi;
	p->tss.es = child_es & 0xffff;					// �μĴ�����16 λ��Ч��
	p->tss.cs = child_cs & 0xffff;
	p->tss.ss = child_ss & 0xffff;
	p->tss.ds = child_ds & 0xffff;
	p->tss.fs = child_fs & 0xffff;
	p->tss.gs = child_gs & 0xffff;
	p->tss.ldt = _LDT(nr);						// ��������nr �ľֲ���������ѡ�����LDT ����������GDT �У���
	p->tss.trace_bitmap = 0x80000000;
	// �����ǰ����ʹ����Э���������ͱ����������ġ�
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	// ����������Ĵ�������ݶλ�ַ���޳�������ҳ�����������������ֵ����0������λ����������
	// ��Ӧ��ͷ�Ϊ�������������ڴ�ҳ��
	if (copy_mem(nr,p)) {						// ���ز�Ϊ0 ��ʾ������
		task[nr] = NULL;
		free_page((long) p);
		return -EAGAIN;
	}
	// ��������������ļ��Ǵ򿪵ģ��򽫶�Ӧ�ļ��Ĵ򿪴�����1��
	for (i=0; i<NR_OPEN;i++)
		if ((f=p->filp[i]))
			f->f_count++;
	// ����ǰ���̣������̣���pwd, root ��executable ���ô�������1��
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	// ��GDT �������������TSS ��LDT ����������ݴ�task �ṹ��ȡ��
	// �������л�ʱ������Ĵ���tr ��CPU �Զ����ء�
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
								/* ����ٽ����������óɿ�����״̬���Է���һ */

	RECORD_TASK_STATE(p->pid, TS_READY, jiffies);
	
	return last_pid;			// �����½��̺ţ���������ǲ�ͬ�ģ���
}

// Ϊ�½���ȡ�ò��ظ��Ľ��̺�last_pid�������������������е������(����index)��
int find_empty_process(void)
{
	int i;

	repeat:
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
			if (task[i] && task[i]->pid == last_pid) goto repeat;
	for(i=1 ; i<NR_TASKS ; i++)
		if (!task[i])
			return i;
	return -EAGAIN;
}