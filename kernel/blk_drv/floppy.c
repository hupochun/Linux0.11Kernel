/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 02.12.91 - Changed to static variables to indicate need for reset
 * and recalibrate. This makes some things easier (output_byte reset
 * checking etc), and means less interrupt jumping in case of errors,
 * so the code is hopefully easier to understand.
 */
 /*
  * 02.12.91 - �޸ĳɾ�̬����������Ӧ��λ������У����������ʹ��ĳЩ����
  * ��������Ϊ���㣨output_byte ��λ���ȣ���������ζ���ڳ���ʱ�ж���ת
  * Ҫ��һЩ������ϣ�������ܸ����ױ����⡣
  */

/*
 * This file is certainly a mess. I've tried my best to get it working,
 * but I don't like programming floppies, and I have only one anyway.
 * Urgel. I should check for more errors, and do more graceful error
 * recovery. Seems there are problems with several drives. I've tried to
 * correct them. No promises. 
 */
/*
 * ����ļ���Ȼ�Ƚϻ��ҡ����Ѿ���������ʹ���ܹ����������Ҳ�ϲ��������̣�
 * ������Ҳֻ��һ�����������⣬��Ӧ��������Ĳ���������Լ���������Ĵ���
 * ����ĳЩ�������������󻹴���һЩ���⡣���Ѿ������Ž��о����ˣ������ܱ�֤
 * ��������ʧ��
 */
/*
 * As with hd.c, all routines within this file can (and will) be called
 * by interrupts, so extreme caution is needed. A hardware interrupt
 * handler may not sleep, or a kernel panic will happen. Thus I cannot
 * call "floppy-on" directly, but have to set a special timer interrupt
 * etc.
 *
 * Also, I'm not certain this works on more than 1 floppy. Bugs may
 * abund.
 */
/*
 * ��ͬhd.c �ļ�һ�������ļ��е������ӳ����ܹ����жϵ��ã�������Ҫ�ر�
 * ��С�ġ�Ӳ���жϴ��������ǲ���˯�ߵģ������ں˾ͻ�ɵ��(����)?����˲���
 * ֱ�ӵ���"floppy-on"����ֻ������һ�������ʱ���жϵȡ�
 *
 * ���⣬�Ҳ��ܱ�֤�ó������ڶ���1 ��������ϵͳ�Ϲ������п��ܴ��ڴ���
 */

#include <linux/sched.h>	// ���ȳ���ͷ�ļ�������������ṹtask_struct����ʼ����0 �����ݣ�
							// ����һЩ�й��������������úͻ�ȡ��Ƕ��ʽ��ຯ������䡣
#include <linux/fs.h>		// �ļ�ϵͳͷ�ļ��������ļ����ṹ��file,buffer_head,m_inode �ȣ���
#include <linux/kernel.h>	// �ں�ͷ�ļ�������һЩ�ں˳��ú�����ԭ�ζ��塣
#include <linux/fdreg.h>	// ����ͷ�ļ����������̿�����������һЩ���塣
#include <asm/system.h>		// ϵͳͷ�ļ������������û��޸�������/�ж��ŵȵ�Ƕ��ʽ���ꡣ
#include <asm/io.h>			// io ͷ�ļ�������Ӳ���˿�����/���������䡣
#include <asm/segment.h>	// �β���ͷ�ļ����������йضμĴ���������Ƕ��ʽ��ຯ����

#define MAJOR_NR 2			// ���������豸����2��
#include "blk.h"			// ���豸ͷ�ļ��������������ݽṹ�����豸���ݽṹ�ͺ꺯������Ϣ��

static int recalibrate = 0;	// ��־����Ҫ����У����
static int reset = 0;		// ��־����Ҫ���и�λ������
static int seek = 0;		// ��־����Ҫִ��Ѱ��������

extern unsigned char current_DOR;	// ��ǰ��������Ĵ���(Digital Output Register)��


// �ֽ�ֱ�������Ƕ�������Ժ꣩����ֵ val ����� port �˿�.
#define immoutb_p(val,port) \
__asm__("outb %0,%1\n\tjmp 1f\n1:\tjmp 1f\n1:"::"a" ((char) (val)),"i" (port))

// �������������ڼ����������豸�š����豸�� = TYPE*4 + DRIVE��
#define TYPE(x) ((x)>>2)	// �������ͣ�2--1.2Mb��7--1.44Mb����
#define DRIVE(x) ((x)&0x03)	// ������ţ�0--3 ��ӦA--D����
/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */
/*
 * ע�⣬���涨��MAX_ERRORS=8 ������ʾ��ÿ�ζ����������8 �� - ��Щ����
 * �Ĵ��󽫰ѳ�������ֵ��2����������ʵ�����ڷ�������֮ǰֻ�賢��5-6 �鼴�ɡ�
 */
#define MAX_ERRORS 8

/*
 * globals used by 'result()'
 */
/* �����Ǻ���'result()'ʹ�õ�ȫ�ֱ��� */
// ��Щ״̬�ֽ��и�����λ�ĺ�����μ�include/linux/fdreg.h ͷ�ļ���
#define MAX_REPLIES 7			// FDC ��෵��7 �ֽڵĽ����Ϣ��
static unsigned char reply_buffer[MAX_REPLIES];
#define ST0 (reply_buffer[0])
#define ST1 (reply_buffer[1])
#define ST2 (reply_buffer[2])
#define ST3 (reply_buffer[3])

/*
 * This struct defines the different floppy types. Unlike minix
 * linux doesn't have a "search for right type"-type, as the code
 * for that is convoluted and weird. I've got enough problems with
 * this driver as it is.
 *
 * The 'stretch' tells if the tracks need to be boubled for some
 * types (ie 360kB diskette in 1.2MB drive etc). Others should
 * be self-explanatory.
 */
/*
 * ��������̽ṹ�����˲�ͬ���������͡���minix ��ͬ���ǣ�linux û��
 * "������ȷ������"-���ͣ���Ϊ���䴦���Ĵ������˷ѽ��ҹֵֹġ�������
 * �Ѿ���������������������ˡ�
 *
 * ��ĳЩ���͵����̣�������1.2MB �������е�360kB ���̵ȣ���'stretch'����
 * ���ŵ��Ƿ���Ҫ���⴦������������Ӧ���������ġ�
 */
 // ���̲����У�
 // size ��С(������)��
 // sect ÿ�ŵ���������
 // head ��ͷ����
 // track �ŵ�����
 // stretch �Դŵ��Ƿ�Ҫ���⴦������־����
 // gap ������϶����(�ֽ���)��
 // rate ���ݴ������ʣ�
 // spec1 ��������4 λ�������ʣ�����λ��ͷж��ʱ�䣩��
static struct floppy_struct {
	unsigned int size, sect, head, track, stretch;
	unsigned char gap,rate,spec1;
} floppy_type[] = {
	{    0, 0,0, 0,0,0x00,0x00,0x00 },	/* no testing */
	{  720, 9,2,40,0,0x2A,0x02,0xDF },	/* 360kB PC diskettes */
	{ 2400,15,2,80,0,0x1B,0x00,0xDF },	/* 1.2 MB AT-diskettes */
	{  720, 9,2,40,1,0x2A,0x02,0xDF },	/* 360kB in 720kB drive */
	{ 1440, 9,2,80,0,0x2A,0x02,0xDF },	/* 3.5" 720kB diskette */
	{  720, 9,2,40,1,0x23,0x01,0xDF },	/* 360kB in 1.2MB drive */
	{ 1440, 9,2,80,0,0x23,0x01,0xDF },	/* 720kB in 1.2MB drive */
	{ 2880,18,2,80,0,0x1B,0x00,0xCF },	/* 1.44MB diskette */
};
/*
 * Rate is 0 for 500kb/s, 2 for 300kbps, 1 for 250kbps
 * Spec1 is 0xSH, where S is stepping rate (F=1ms, E=2ms, D=3ms etc),
 * H is head unload time (1=16ms, 2=32ms, etc)
 *
 * Spec2 is (HLD<<1 | ND), where HLD is head load time (1=2ms, 2=4 ms etc)
 * and ND is set means no DMA. Hardcoded to 6 (HLD=6ms, use DMA).
 */
/*
 * ��������rate��0 ��ʾ500kb/s��1 ��ʾ300kbps��2 ��ʾ250kbps��
 * ����spec1 ��0xSH������S �ǲ������ʣ�F-1 ���룬E-2ms��D=3ms �ȣ���
 * H �Ǵ�ͷж��ʱ�䣨1=16ms��2=32ms �ȣ�
 *
 * spec2 �ǣ�HLD<<1 | ND��������HLD �Ǵ�ͷ����ʱ�䣨1=2ms��2=4ms �ȣ�
 * ND ��λ��ʾ��ʹ��DMA��No DMA�����ڳ�����Ӳ�����6��HLD=6ms��ʹ��DMA����
 */

extern void floppy_interrupt(void);
extern char tmp_floppy_area[1024];

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
/*
 * ������һЩȫ�ֱ�������Ϊ���ǽ���Ϣ�����жϳ�����򵥵ķ�ʽ��������
 * ���ڵ�ǰ��������ݡ�
 */
static int cur_spec1 = -1;
static int cur_rate = -1;
static struct floppy_struct * floppy = floppy_type;
static unsigned char current_drive = 0;
static unsigned char sector = 0;
static unsigned char head = 0;
static unsigned char track = 0;
static unsigned char seek_track = 0;
static unsigned char current_track = 255;
static unsigned char command = 0;
unsigned char selected = 0;
struct task_struct * wait_on_floppy_select = NULL;

//// �ͷţ�ȡ��ѡ���ģ����̣���������
// ��������Ĵ���(DOR)�ĵ�2 λ����ָ��ѡ���������0-3 ��ӦA-D����
void floppy_deselect(unsigned int nr)
{
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0;
	wake_up(&wait_on_floppy_select);
}

/*
 * floppy-change is never called from an interrupt, so we can relax a bit
 * here, sleep etc. Note that floppy-on tries to set current_DOR to point
 * to the desired drive, but it will probably not survive the sleep if
 * several floppies are used at the same time: thus the loop.
 */
/*
 * floppy-change()���Ǵ��жϳ����е��õģ������������ǿ�������һ�£�˯���ȡ�
 * ע��floppy-on()�᳢������current_DOR ָ�������������������ͬʱʹ�ü���
 * ����ʱ����˯�ߣ���˴�ʱֻ��ʹ��ѭ����ʽ��
 */
 //// ���ָ�����������̸��������������̸������򷵻�1�����򷵻�0��
int floppy_change(unsigned int nr)
{
repeat:
	floppy_on(nr);							// ����ָ������nr��kernel/sched.c����
	// �����ǰѡ�����������ָ��������nr�������Ѿ�ѡ�����������������õ�ǰ���������ж�
	// �ȴ�״̬��
	while ((current_DOR & 3) != nr && selected)
		interruptible_sleep_on(&wait_on_floppy_select);
	// �����ǰû��ѡ�������������ߵ�ǰ���񱻻���ʱ����ǰ������Ȼ����ָ��������nr����ѭ���ȴ���
	if ((current_DOR & 3) != nr)
		goto repeat;
	// ȡ��������Ĵ���ֵ��������λ��λ7����λ�����ʾ�����Ѹ�������ʱ�ر����ﲢ�˳�����1��
	// ����ر������˳�����0��
	if (inb(FD_DIR) & 0x80) {
		floppy_off(nr);
		return 1;
	}
	floppy_off(nr);
	return 0;
}

//// �����ڴ�顣
#define copy_buffer(from,to) \
__asm__("cld ; rep ; movsl" \
	::"c" (BLOCK_SIZE/4),"S" ((long)(from)),"D" ((long)(to)) \
	)

//// ���ã���ʼ��������DMA ͨ����
static void setup_DMA(void)
{
	long addr = (long) CURRENT->buffer;		// ��ǰ��������������ڴ���λ�ã���ַ����

	cli();
	// ��������������ڴ�1M ���ϵĵط�����DMA ������������ʱ��������(tmp_floppy_area ����)
	// (��Ϊ8237A оƬֻ����1M ��ַ��Χ��Ѱַ)�������д��������轫���ݸ��Ƶ�����ʱ����
	if (addr >= 0x100000) {
		addr = (long) tmp_floppy_area;
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer,tmp_floppy_area);
	}
	/* mask DMA 2 */
	// ��ͨ�����μĴ����˿�Ϊ0x10��λ0-1 ָ��DMA ͨ��(0--3)��λ2��1 ��ʾ���Σ�0 ��ʾ��������
	immoutb_p(4|2,10);
	/* output command byte. I don't know why, but everyone (minix, */
	/* sanches & canton) output this twice, first to 12 then to 11 */
	/* ��������ֽڡ����ǲ�֪��Ϊʲô������ÿ���ˣ�minix��*/
	/* sanches ��canton����������Σ�������12 �ڣ�Ȼ����11 �� */
	// ����Ƕ���������DMA �������˿�12 ��11 д��ʽ�֣�����0x46��д��0x4A����
 	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
	"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:"::
	"a" ((char) ((command == FD_READ)?DMA_READ:DMA_WRITE)));
	/* 8 low bits of addr *//* ��ַ��0-7 λ */
	// ��DMA ͨ��2 д���/��ǰ��ַ�Ĵ������˿�4����
	immoutb_p(addr,4);
	addr >>= 8;
	/* bits 8-15 of addr *//* ��ַ��8-15 λ */
	immoutb_p(addr,4);
	addr >>= 8;
	/* bits 16-19 of addr *//* ��ַ16-19 λ */
	// DMA ֻ������1M �ڴ�ռ���Ѱַ�����16-19 λ��ַ�����ҳ��Ĵ���(�˿�0x81)��
	immoutb_p(addr,0x81);
	/* low 8 bits of count-1 (1024-1=0x3ff) *//* ��������8 λ(1024-1=0x3ff) */
	// ��DMA ͨ��2 д���/��ǰ�ֽڼ�����ֵ���˿�5����
	immoutb_p(0xff,5);
	/* high 8 bits of count-1 *//* ��������8 λ */
	// һ�ι�����1024 �ֽڣ�������������
	immoutb_p(3,5);
	/* activate DMA 2 *//* ����DMA ͨ��2 ������ */
	// ��λ��DMA ͨ��2 �����Σ�����DMA2 ����DREQ �źš�
	immoutb_p(0|2,10);
	sti();
}

//// �����̿��������һ���ֽ����ݣ�������������
static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (reset)
		return;
	// ѭ����ȡ��״̬������FD_STATUS(0x3f4)��״̬�����״̬��STATUS_READY ����STATUS_DIR=0
	// (CPU->FDC)���������ݶ˿����ָ���ֽڡ�
	for(counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY) {
			outb(byte,FD_DATA);
			return;
		}
	}
	// �����ѭ��1 ��ν��������ܷ��ͣ����ø�λ��־������ӡ������Ϣ��
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

//// ��ȡFDC ִ�еĽ����Ϣ��
// �����Ϣ���7 ���ֽڣ������reply_buffer[]�С����ض���Ľ���ֽ�����������ֵ=-1
// ��ʾ������
static int result(void)
{
	int i = 0, counter, status;

	if (reset)
		return -1;
	for (counter = 0 ; counter < 10000 ; counter++) {
		status = inb_p(FD_STATUS)&(STATUS_DIR|STATUS_READY|STATUS_BUSY);
		if (status == STATUS_READY)
			return i;
		if (status == (STATUS_DIR|STATUS_READY|STATUS_BUSY)) {
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	reset = 1;
	printk("Getstatus times out\n\r");
	return -1;
}

//// ���̲��������жϵ��ú������������жϴ���������á�
static void bad_flp_intr(void)
{
	CURRENT->errors++;		// ��ǰ���������������1��
	// �����ǰ������������������������������������ȡ��ѡ����ǰ������������������������£���
	if (CURRENT->errors > MAX_ERRORS) {
		floppy_deselect(current_drive);
		end_request(0);
	}
	// �����ǰ����������������������������������һ�룬���ø�λ��־������������и�λ������
	// Ȼ�����ԡ���������������У��һ�£����ԡ�
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
	else
		recalibrate = 1;
}	

/*
 * Ok, this interrupt is called after a DMA read/write has succeeded,
 * so we check the results, and copy any buffers.
 */
/*
 * OK��������жϴ�����������DMA ��/д�ɹ�����õģ��������ǾͿ��Լ��ִ�н����
 * �����ƻ������е����ݡ�
 */
//// ���̶�д�����ɹ��жϵ��ú�������
static void rw_interrupt(void)
{
	// ������ؽ���ֽ���������7������״̬�ֽ�0��1 ��2 �д��ڳ�����־��������д����
	// ����ʾ������Ϣ���ͷŵ�ǰ����������������ǰ����������ִ�г�������������
	// Ȼ�����ִ���������������
	// ( 0xf8 = ST0_INTR | ST0_SE | ST0_ECE | ST0_NR )
	// ( 0xbf = ST1_EOC | ST1_CRC | ST1_OR | ST1_ND | ST1_WP | ST1_MAM��Ӧ����0xb7)
	// ( 0x73 = ST2_CM | ST2_CRC | ST2_WC | ST2_BC | ST2_MAM )
	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73)) {
		if (ST1 & 0x02) {
			printk("Drive %d is write protected\n\r",current_drive);
			floppy_deselect(current_drive);
			end_request(0);
		} else
			bad_flp_intr();
		do_fd_request();
		return;
	}
	// �����ǰ������Ļ�����λ��1M ��ַ���ϣ���˵���˴����̶����������ݻ�������ʱ�������ڣ�
	// ��Ҫ���Ƶ�������Ļ������У���ΪDMA ֻ����1M ��ַ��ΧѰַ����
	if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area,CURRENT->buffer);
	// �ͷŵ�ǰ���̣�������ǰ������ø��±�־�����ټ���ִ���������������
	floppy_deselect(current_drive);
	end_request(1);
	do_fd_request();
}

//// ����DMA ��������̲�������Ͳ��������1 �ֽ�����+ 0~7 �ֽڲ�������
inline void setup_rw_floppy(void)
{
	setup_DMA ();				// ��ʼ������DMA ͨ����
  	do_floppy = rw_interrupt;	// �������жϵ��ú���ָ�롣
  	output_byte (command);		// ���������ֽڡ�
  	output_byte (head << 2 | current_drive);	// ���Ͳ�������ͷ��+�������ţ���
  	output_byte (track);		// ���Ͳ������ŵ��ţ���
  	output_byte (head);			// ���Ͳ�������ͷ�ţ���
  	output_byte (sector);		// ���Ͳ�������ʼ�����ţ���
  	output_byte (2);			/* sector size = 512 */
  								// ���Ͳ���(�ֽ���(N=2)512 �ֽ�)��
  	output_byte (floppy->sect);	// ���Ͳ�����ÿ�ŵ�����������
  	output_byte (floppy->gap);	// ���Ͳ���������������ȣ���
  	output_byte (0xFF);			/* sector size (0xff when n!=0 ?) */
	// ���Ͳ�������N=0 ʱ������������ֽڳ��ȣ����������á�
	// ���ڷ�������Ͳ���ʱ�������������ִ����һ���̲�������
	if (reset)
		do_fd_request();
}

/*
 * This is the routine called after every seek (or recalibrate) interrupt
 * from the floppy controller. Note that the "unexpected interrupt" routine
 * also does a recalibrate, but doesn't come here.
 */
/*
 * ���ӳ�������ÿ�����̿�����Ѱ����������У�����жϺ󱻵��õġ�ע��
 * "unexpected interrupt"(�����ж�)�ӳ���Ҳ��ִ������У�������������ڴ˵ء�
 */
 
 //// Ѱ�������жϵ��ú�����
 // ���ȷ��ͼ���ж�״̬������״̬��ϢST0 �ʹ�ͷ���ڴŵ���Ϣ����������ִ�д������
 // ��⴦����ȡ���������̲���������������״̬��Ϣ���õ�ǰ�ŵ�������Ȼ����ú���
 // setup_rw_floppy()����DMA ��������̶�д����Ͳ�����
static void seek_interrupt(void)
{
	/* sense drive status *//* ����ж�״̬ */
	// ���ͼ���ж�״̬�������������������ؽ����Ϣ�����ֽڣ�ST0 �ʹ�ͷ��ǰ�ŵ��š�
	output_byte(FD_SENSEI);
	// ������ؽ���ֽ���������2������ST0 ��ΪѰ�����������ߴ�ͷ���ڴŵ�(ST1)�������趨�ŵ���
	// ��˵�������˴�������ִ�м��������������Ȼ�����ִ��������������˳���
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track) {
		bad_flp_intr();
		do_fd_request();
		return;
	}
	current_track = ST1;			// ���õ�ǰ�ŵ���
  	setup_rw_floppy ();				// ����DMA ��������̲�������Ͳ�����
}

/*
 * This routine is called when everything should be correctly set up
 * for the transfer (ie floppy motor is on and the correct floppy is
 * selected).
 */
/*
 * �ú������ڴ��������������Ϣ����ȷ���úú󱻵��õģ�Ҳ�����������ѿ���
 * ������ѡ������ȷ�����̣���������
 */
 //// ��д���ݴ��亯����
static void transfer(void)
{
	// ���ȿ���ǰ�����������Ƿ����ָ���������Ĳ����������Ǿͷ����������������������Ӧ
	// ����������1����4 λ�������ʣ�����λ��ͷж��ʱ�䣻����2����ͷ����ʱ�䣩��
	if (cur_spec1 != floppy->spec1) {
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY);			// �������ô��̲������
		output_byte(cur_spec1);				/* hut etc */// ���Ͳ���
		output_byte(6);						/* Head load time =6ms, DMA */
	}
	// �жϵ�ǰ���ݴ��������Ƿ���ָ����������һ�£������Ǿͷ���ָ������������ֵ�����ݴ���
	// ���ʿ��ƼĴ���(FD_DCR)��
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate,FD_DCR);
	// �����ؽ����Ϣ�������������ٵ��������������������ء�
	if (reset) {
		do_fd_request();
		return;
	}
	// ��Ѱ����־Ϊ�㣨����ҪѰ������������DMA ��������Ӧ��д��������Ͳ�����Ȼ�󷵻ء�
	if (!seek) {
		setup_rw_floppy();
		return;
	}
	// ����ִ��Ѱ���������������жϴ������ú���ΪѰ���жϺ�����
	do_floppy = seek_interrupt;
	// �����ʼ�ŵ��Ų����������ʹ�ͷѰ������Ͳ���
	if (seek_track) {
		output_byte(FD_SEEK);					// ���ʹ�ͷѰ�����
		output_byte(head<<2 | current_drive);	//���Ͳ�������ͷ��+��ǰ�����š�
		output_byte(seek_track);				// ���Ͳ������ŵ��š�
	} else {
		output_byte(FD_RECALIBRATE);			// ��������У�����
		output_byte(head<<2 | current_drive);	//���Ͳ�������ͷ��+��ǰ�����š�
	}
	// �����λ��־����λ�������ִ�����������
	if (reset)
		do_fd_request();
}

/*
 * Special case - used after a unexpected interrupt (or reset)
 */
/*
 * ������� - ���������жϣ���λ��������
 */
 //// ��������У���жϵ��ú�����
 // ���ȷ��ͼ���ж�״̬����޲�������������ؽ���������������ø�λ��־������λ����
 // У����־��Ȼ���ٴ�ִ����������
static void recal_interrupt(void)
{
	output_byte(FD_SENSEI);					// ���ͼ���ж�״̬���
	if (result()!=2 || (ST0 & 0xE0) == 0x60)// ������ؽ���ֽ���������2 ������
		reset = 1;							// �쳣���������ø�λ��־��
	else									// ����λ����У����־��
		recalibrate = 0;
	do_fd_request();						// ִ�����������
}

//// ���������ж������жϵ��ú�����
// ���ȷ��ͼ���ж�״̬����޲�������������ؽ���������������ø�λ��־������������
// У����־��
void unexpected_floppy_interrupt(void)
{
	output_byte(FD_SENSEI);					// ���ͼ���ж�״̬���
	if (result()!=2 || (ST0 & 0xE0) == 0x60)// ������ؽ���ֽ���������2 ������
		reset = 1;							// �쳣���������ø�λ��־��
	else									// ����������У����־��
		recalibrate = 1;
}

//// ��������У������������
// �����̿�����FDC ��������У������Ͳ���������λ����У����־��
static void recalibrate_floppy(void)
{
	recalibrate = 0;						// ��λ����У����־��
	current_track = 0;						// ��ǰ�ŵ��Ź��㡣
	do_floppy = recal_interrupt;			// �������жϵ��ú���ָ��ָ������У�����ú�����
	output_byte(FD_RECALIBRATE);			// �����������У����
	output_byte(head<<2 | current_drive);	// ���Ͳ���������ͷ�żӣ���ǰ��������
	if (reset)								// �������(��λ��־����λ)�����ִ����������
		do_fd_request();
}

//// ���̿�����FDC ��λ�жϵ��ú������������жϴ��������е��á�
// ���ȷ��ͼ���ж�״̬����޲�������Ȼ��������صĽ���ֽڡ����ŷ����趨������������
// ����ز���������ٴε���ִ����������
static void reset_interrupt(void)
{
	output_byte(FD_SENSEI);					// ���ͼ���ж�״̬���
	(void) result();						// ��ȡ����ִ�н���ֽڡ�
	output_byte(FD_SPECIFY);				// �����趨�����������
	output_byte(cur_spec1);		/* hut etc */// ���Ͳ�����
	output_byte(6);			/* Head load time =6ms, DMA */
	do_fd_request();						// ����ִ����������
}

/*
 * reset is done by pulling bit 2 of DOR low for a while.
 */
/* FDC ��λ��ͨ������������Ĵ���(DOR)λ2 ��0 һ���ʵ�ֵ� */
//// ��λ���̿�������
static void reset_floppy(void)
{
	int i;

	reset = 0;								// ��λ��־��0��
	cur_spec1 = -1;
	cur_rate = -1;
	recalibrate = 1;						// ����У����־��λ��
	printk("Reset-floppy called\n\r");		// ��ʾִ�����̸�λ������Ϣ��
	cli();
	do_floppy = reset_interrupt;			// �����������жϴ��������е��õĺ�����
	outb_p(current_DOR & ~0x04,FD_DOR);		// �����̿�����FDC ִ�и�λ������
	for (i=0 ; i<100 ; i++)					// �ղ������ӳ١�
		__asm__("nop");
	outb(current_DOR,FD_DOR);				// ���������̿�������
	sti();
}

//// ����������ʱ�жϵ��ú�����
// ���ȼ����������Ĵ���(DOR)��ʹ��ѡ��ǰָ������������Ȼ�����ִ�����̶�д����
// ����transfer()��
static void floppy_on_interrupt(void)
{
/* We cannot do a floppy-select, as that might sleep. We just force it */
/* ���ǲ�����������ѡ�����������Ϊ���������ܻ��������˯�ߡ�����ֻ����ʹ���Լ�ѡ�� */
  		selected = 1;						// ����ѡ��ǰ��������־��
// �����ǰ������������������Ĵ���DOR �еĲ�ͬ������������DOR Ϊ��ǰ������current_drive��
// ��ʱ�ӳ�2 ���δ�ʱ�䣬Ȼ��������̶�д���亯��transfer()������ֱ�ӵ������̶�д���亯����
	if (current_drive != (current_DOR & 3)) {
		current_DOR &= 0xFC;
		current_DOR |= current_drive;
		outb(current_DOR,FD_DOR);			// ����������Ĵ��������ǰDOR��
		add_timer(2,&transfer);				// ���Ӷ�ʱ����ִ�д��亯����
	} else
		transfer();							// ִ�����̶�д���亯����
}

//// ���̶�д�������������
//
void do_fd_request(void)
{
	unsigned int block;

	seek = 0;
	// �����λ��־����λ����ִ�����̸�λ�����������ء�
	if (reset) {
		reset_floppy();
		return;
	}
	// �������У����־����λ����ִ����������У�������������ء�
	if (recalibrate) {
		recalibrate_floppy();
		return;
	}
	INIT_REQUEST;										// ���������ĺϷ���(�μ�kernel/blk_drv/blk.h)��
	floppy = (MINOR(CURRENT->dev)>>2) + floppy_type;	// ��������ṹ�������豸���е���������(MINOR(CURRENT->dev)>>2)��Ϊ����ȡ�����̲����顣
	// �����ǰ������������������ָ���������������ñ�־ seek����ʾ��Ҫ����Ѱ��������
	// Ȼ�����������豸Ϊ��ǰ��������
	if (current_drive != CURRENT_DEV)
		seek = 1;
	current_drive = CURRENT_DEV;
	// ���ö�д��ʼ��������Ϊÿ�ζ�д���Կ�Ϊ��λ��1 ��2 ����������������ʼ������Ҫ�����
	// ������������С2 ����������������ô����������ִ����һ�������
	block = CURRENT->sector;							// ȡ��ǰ��������������ʼ�����š�
	if (block+2 > floppy->size) {						// ���block+2 ���ڴ���������������
		end_request(0);									// �����������������
		goto repeat;
	}
	// ���Ӧ�ڴŵ��ϵ������ţ���ͷ�ţ��ŵ��ţ���Ѱ�ŵ��ţ�������������ͬ��ʽ���̣���
	sector = block % floppy->sect;						// ��ʼ������ÿ�ŵ�������ȡģ���ôŵ��������š�
  	block /= floppy->sect;								// ��ʼ������ÿ�ŵ�������ȡ��������ʼ�ŵ�����
  	head = block % floppy->head;						// ��ʼ�ŵ����Դ�ͷ��ȡģ���ò����Ĵ�ͷ�š�
  	track = block / floppy->head;						// ��ʼ�ŵ����Դ�ͷ��ȡ�����ò����Ĵŵ��š�
  	seek_track = track << floppy->stretch;				// ��Ӧ���������������ͽ��е�������Ѱ���š�
	// ���Ѱ�����뵱ǰ��ͷ���ڴŵ���ͬ��������ҪѰ����־seek��
	if (seek_track != current_track)
		seek = 1;
	sector++;											// ������ʵ�����������Ǵ�1 ����
	if (CURRENT->cmd == READ)							// ������������Ƕ��������������̶������롣
		command = FD_READ;
	else if (CURRENT->cmd == WRITE)						// �������������д��������������д�����롣
		command = FD_WRITE;
	else
		panic("do_fd_request: unknown command");
	// ���Ӷ�ʱ��������ָ�������������������������ӳٵ�ʱ�䣨�δ�����������ʱʱ�䵽ʱ�͵���
	// ����floppy_on_interrupt()
	add_timer(ticks_to_floppy_on(current_drive),&floppy_on_interrupt);
}

//// ����ϵͳ��ʼ����
// �������̿��豸������������(do_fd_request())�������������ж���(int 0x26����ӦӲ��
// �ж������ź�IRQ6����Ȼ��ȡ���Ը��ж��źŵ����Σ��������̿�����FDC �����ж������źš�
void floppy_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;		//  = do_fd_request()��
	set_trap_gate(0x26,&floppy_interrupt);				// ���������ж��� int 0x26(38)��
	outb(inb_p(0x21)&~0x40,0x21);						// ��λ���̵��ж���������λ������
}