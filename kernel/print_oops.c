/*
 *
 * Copyright (C) 2014 Teodora Baluta <teobaluta@gmail.com>
 * Copyright (C) 2014 Levente Kurusa <levex@linux.com>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#include <linux/print_oops.h>
#include <linux/kdebug.h>
#include <linux/bug.h>
#include <linux/qrencode.h>
#include <linux/fb.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/jiffies.h>

#define COMPR_LEVEL 9
#define QQQ_WHITE 0x0F
#define QQQ_BLACK 0x00

static int qr_oops_cmd = 0;
core_param(qr_oops_cmd, qr_oops_cmd, int, 0644);

static int qr_oops_param0 = 0;
core_param(qr_oops_param0, qr_oops_param0, int, 0644);

static int qr_oops_param1 = 0;
core_param(qr_oops_param1, qr_oops_param1, int, 0644);

static char qr_buffer[MESSAGE_BUFSIZE];
static int buf_pos;

static DEFINE_MUTEX(compr_mutex);
static DEFINE_MUTEX(qr_list_mutex);
static struct z_stream_s stream;

static int compr_init(void)
{
	size_t size = max(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL),
			zlib_inflate_workspacesize());
	stream.workspace = (char*)vmalloc(size);
	if (!stream.workspace)
		return -ENOMEM;
	return 0;
}

static void compr_exit(void)
{
	vfree(stream.workspace);
}

static int compress(void *in, void *out, size_t inlen, size_t outlen)
{
	int ret;

	ret = compr_init();
	if (ret != 0)
		goto error;

	mutex_lock(&compr_mutex);
	ret = zlib_deflateInit(&stream, COMPR_LEVEL);
	if (ret != Z_OK)
	{
		printk("qr_compress: zlib_deflateInit failed with ret = %d\n",
		       ret);
		goto error;
	}

	stream.next_in = in;
	stream.avail_in = inlen;
	stream.total_in = 0;
	stream.next_out = out;
	stream.avail_out = outlen;
	stream.total_out = 0;

	ret = zlib_deflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END)
	{
		printk("qr_compress: zlib_deflate failed with ret = %d\n", ret);
		goto error;
	}

	ret = zlib_deflateEnd(&stream);
	if (ret != Z_OK)
	{
		printk("qr_compress: zlib_deflateEnd failed with ret = %d\n",
		       ret);
		goto error;
	}

	if (stream.total_out >= stream.total_in)
	{
		printk(KERN_EMERG "qr_compress: total_out > total_in\n");
	}

	ret = stream.total_out;
error:
	mutex_unlock(&compr_mutex);
	return ret;
}

static inline int compute_w(struct fb_info *info, int qrw)
{
	int xres  = info->var.xres;
	int yres  = info->var.yres;
	int minxy = (xres < yres) ? xres : yres;
	int ret = minxy-minxy/4;

	return ret/qrw;
}

static inline void draw_qr(struct fb_info *info, struct qrcode *qr,
			   int pos_x, int pos_y,
			   int cell_width, int cell_height,
			   int border)
{
	int i, j;
	int is_black;
	struct fb_fillrect rect;

	rect.width = cell_width;
	rect.height = cell_height;
	rect.rop = 0;

	if(border) {
		/* Print borders: */
		rect.color = QQQ_WHITE;
		for (i = 0; i < qr->width + 2; i++) {
			/* Top */
			rect.dx = 0 + pos_x;
			rect.dy = i * cell_height + pos_y;
			cfb_fillrect(info, &rect);

			/* Bottom */
			rect.dx = (qr->width + 1) * cell_width + pos_x;
			rect.dy = i * cell_height + pos_y;
			cfb_fillrect(info, &rect);

			/* Left */
			rect.dx = i * cell_width + pos_x;
			rect.dy = pos_y;
			cfb_fillrect(info, &rect);

			/* Right */
			rect.dx = i * cell_width + pos_x;
			rect.dy = (qr->width + 1) * cell_height + pos_y;
			cfb_fillrect(info, &rect);
		}
	}

	/* Print actual QR matrix: */
	for (i = 0; i < qr->width; i++) {
		for (j = 0; j < qr->width; j++) {
			rect.dx = (j + 1) * cell_width + pos_x;
			rect.dy = (i + 1) * cell_height + pos_y;
			is_black = qr->data[i * qr->width + j] & 1;
			rect.color = is_black ? QQQ_BLACK : QQQ_WHITE;
			cfb_fillrect(info, &rect);
		}
	}
}

#define ASCII_BLACK " "
#define ASCII_BLOCK "%c", 219
#define ASCII_HALFBLOCK_TOP	"%c", 223
#define ASCII_HALFBLOCK_BOTTOM	"%c", 220

static inline int qr_is_black(struct qrcode *qr, int x, int y)
{
	if(x < 0 || y < 0 || x >= qr->width || y >= qr->width)
	{
		return 0;
	}
	return qr->data[x * qr->width + y] & 1;
}

static inline void draw_ascii_qr(struct qrcode *qr)
{
	int i, j;
	int up, down;

	/* Print actual QR matrix: */
	for (i = -1; i < qr->width + 1; i+=2) {
		for (j = -1; j < qr->width + 1; j++) {
			up = 1 - qr_is_black(qr, i, j);
			down = 1 - qr_is_black(qr, i + 1, j);
			if(up)
				if(down)
					printk(ASCII_BLOCK);
				else
					printk(ASCII_HALFBLOCK_TOP);
			else
				if(down)
					printk(ASCII_HALFBLOCK_BOTTOM);
				else
					printk(ASCII_BLACK);
		}
		printk("\n");
	}
}

struct qr_list_element
{
	struct qr_list_element *next;
	struct qr_list_element *prev;

	struct qrcode *qr;
	int message_id;
	int packet_id;
};

static struct qr_list_element *qr_list_head = NULL;

static struct task_struct *qr_thread;

#define BUFFER_SIZE	4*1024

static inline void qr_list_push(struct qr_list_element *element)
{
	if (!qr_list_head) {
		qr_list_head = element;
		qr_list_head->next = element;
		qr_list_head->prev = element;
	} else {
		qr_list_head->prev->next = element;
		element->prev = qr_list_head->prev;
		element->next = qr_list_head;
		qr_list_head->prev = element;
	}
}

static inline struct qr_list_element *qr_list_delete(
	struct qr_list_element *element)
{
	struct qr_list_element *next;
	next = element->next;

	element->prev->next = element->next;
	element->next->prev = element->prev;

	if (qr_list_head == element)
	{
		if(element->prev == element) {
			qr_list_head = 0;
			next = 0;
		} else {
			qr_list_head = element->prev;
		}
	}

	vfree(element);

	return next;
}

static void qr_list_delete_message(int message_id)
{
	struct qr_list_element *it;

	mutex_lock(&qr_list_mutex);
	it = qr_list_head;
	do {
		if (it->message_id == message_id)
			it = qr_list_delete(it);
		else
			it = it->next;
	} while (it != qr_list_head);
	if (qr_list_head && qr_list_head->message_id == message_id)
		qr_list_delete(qr_list_head);
	mutex_unlock(&qr_list_mutex);
}

static void qr_list_clear(void)
{
	struct qr_list_element *it;

	mutex_lock(&qr_list_mutex);
	it = qr_list_head;
	do {
		it = qr_list_delete(it);
	} while (it != qr_list_head);
	if (qr_list_head)
		qr_list_delete(qr_list_head);
	mutex_unlock(&qr_list_mutex);
}

static void qr_list_delete_packet(int message_id, int packet_id)
{
	struct qr_list_element *it;

	mutex_lock(&qr_list_mutex);
	it = qr_list_head;
	do {
		if (it->message_id == message_id && it->packet_id == packet_id)
			it = qr_list_delete(it);
		else
			it = it->next;
	} while (it != qr_list_head);
	mutex_unlock(&qr_list_mutex);
}

#define BK1_MAGIC_FIRSTBYTE 222
#define BK1_MAGIC_SECONDBYTE 173

#define BK1_ENCODE_NONE 0
#define BK1_ENCODE_DEFLATE 1

#define BK1_VERSION 0

static void make_bk1_packet(char *start, int len, int message_id,
		     	    int packet_id, int packet_count)
{
	struct qr_list_element *element;
	int header_size;
	ssize_t packet_len;
	char compr_packet_buffer[BUFFER_SIZE];
	char checksum;

	element = (struct qr_list_element*)vmalloc(sizeof(*element));
	element->message_id = message_id;
	element->packet_id = packet_id;

	header_size = 0;

	checksum = BK1_VERSION ^ message_id ^ packet_count ^ packet_id ^
		   BK1_ENCODE_DEFLATE;

	compr_packet_buffer[header_size] = BK1_MAGIC_FIRSTBYTE;
	++header_size;
	compr_packet_buffer[header_size] = BK1_MAGIC_SECONDBYTE;
	++header_size;
	compr_packet_buffer[header_size] = BK1_VERSION;
	++header_size;
	compr_packet_buffer[header_size] = message_id;
	++header_size;
	compr_packet_buffer[header_size] = packet_count;
	++header_size;
	compr_packet_buffer[header_size] = packet_id;
	++header_size;
	compr_packet_buffer[header_size] = BK1_ENCODE_DEFLATE;
	++header_size;
	compr_packet_buffer[header_size] = checksum;
	++header_size;
	compr_packet_buffer[header_size] = (len >> 8) & 0xFF;
	++header_size;
	compr_packet_buffer[header_size] = len & 0xFF;
	++header_size;

	packet_len = compress(start, compr_packet_buffer + header_size, len,
			      BUFFER_SIZE - header_size);
	if (packet_len < 0) {
		printk(KERN_EMERG "Compression of QR code failed packet_len=%zd\n",
			packet_len);
		goto ERROR;
	}
	compr_exit();

	packet_len += header_size;

	element->qr = qrcode_encode_data(packet_len, compr_packet_buffer, 0,
					 QR_ECLEVEL_H);
	if (!element->qr) {
		printk(KERN_EMERG "Failed to encode data as a QR code!\n");
		goto ERROR;
	}

	mutex_lock(&qr_list_mutex);
	qr_list_push(element);
	mutex_unlock(&qr_list_mutex);

	return;
ERROR:
	printk(KERN_EMERG "Failed to make QR message packet!\n");
}

struct qr_list_element *tar_slow, *tar_fast, *tar_current;
int tar_step;

static inline void tar_strategy_next_step(void)
{
	if (!qr_list_head)
		return;

	++tar_step;
	if (tar_step == 4)
		tar_step = 0;

	if (tar_step == 0) {
		mutex_lock(&qr_list_mutex);
		tar_slow = tar_slow->next;
		mutex_unlock(&qr_list_mutex);
		tar_current = tar_slow;
	} else if(tar_step == 1) {
		tar_current = tar_slow;
	} else if(tar_step == 2) {
		mutex_lock(&qr_list_mutex);
		tar_fast = tar_fast->next;
		mutex_unlock(&qr_list_mutex);
		tar_current = tar_fast;
	} else if(tar_step == 3) {
		mutex_lock(&qr_list_mutex);
		tar_fast = tar_fast->next;
		mutex_unlock(&qr_list_mutex);
		tar_current = tar_fast;
	}
}

static inline void tar_strategy_init(void)
{
	tar_step = 0;
	tar_slow = qr_list_head;
	tar_fast = qr_list_head;
	tar_current = qr_list_head;
}

static inline struct qrcode *tar_strategy_get_qrcode(void)
{
	if (tar_current)
		return tar_current->qr;
	return 0;
}

#define MESSAGE_DEFAULT_PACKET_SIZE 300

static DEFINE_MUTEX(message_count_mutex);

static int message_count = 0;

static void make_bk1_message(void)
{
	int remaining_bytes;
	int packet_length;
	int left;
	int packet_count;
	int packet_id;
	int message_id;

	mutex_lock(&message_count_mutex);
	++message_count;
	message_id = message_count;
	mutex_unlock(&message_count_mutex);

	packet_count = buf_pos / MESSAGE_DEFAULT_PACKET_SIZE;
	if(buf_pos % MESSAGE_DEFAULT_PACKET_SIZE)
		++packet_count;

	left = 0;
	packet_id = 0;
	while(left < buf_pos) {
		remaining_bytes = buf_pos - left;
		packet_length = MESSAGE_DEFAULT_PACKET_SIZE;
		if(packet_length > remaining_bytes)
			packet_length = remaining_bytes;
		++packet_id;
		make_bk1_packet(qr_buffer + left, packet_length, message_id,
				packet_id, packet_count);
		left += packet_length;
	}
}

static void print_messages(void)
{
	struct qr_list_element *it = qr_list_head;
	int last_message_id = -1;

	printk("QR: ids of messages in queue: ");
	do {
		if (it->message_id != last_message_id) {
			last_message_id = it->message_id;
			printk("%d ", last_message_id);
		}
		it = it->next;
	} while (it != qr_list_head);
	printk("\n");
}

static void print_packets(void)
{
	struct qr_list_element *it = qr_list_head;

	printk("QR: packets in queue <message, packet>: ");
	do {
		printk("<%d, %d> ", it->message_id, it->packet_id);
		it = it->next;
	} while (it != qr_list_head);
	printk("\n");
}

static void print_packets_by_msg(int message_id)
{
	struct qr_list_element *it = qr_list_head = qr_list_head;

	printk("QR: packets in queue for message with id %d: ", message_id);
	do {
		if (it->message_id == message_id)
			printk("%d ", it->packet_id);
		it = it->next;
	} while (it != qr_list_head);
	printk("\n");
}

static struct fb_info *info;
static struct fb_fillrect rect;
static int qr_total_width = 0;
static int qr_offset_x = 0;
static int qr_offset_y = 0;

static void clear_last_qr(void)
{
	if(info) {
		printk("QR: framebuffer clear\n");

		console_lock();

		rect.width = qr_total_width;
		rect.height = qr_total_width;
		rect.dx = qr_offset_x;
		rect.dy = qr_offset_y;
		rect.rop = 0;
		rect.color = QQQ_BLACK;
		cfb_fillrect(info, &rect);

		console_unlock();
	}
}

#define QR_OOPS_CMD_NOTHING 0
#define QR_OOPS_CMD_PRINT_MESSAGES 1
#define QR_OOPS_CMD_PRINT_PACKETS 2
#define QR_OOPS_CMD_DELETE_MESSAGE 3
#define QR_OOPS_CMD_DELETE_PACKET 4
#define QR_OOPS_CMD_PAUSE 5
#define QR_OOPS_CMD_RESUME 6
#define QR_OOPS_CMD_CLEAR_QUEUE 7
#define QR_OOPS_CMD_STOP 8
#define QR_THREAD_TIME_STEP 750*1000

int qr_thread_func(void* data)
{
	struct qrcode *curr_qr = 0;
	int w;
	int last_time;
	int time_accumulator;
	int time_now;
	int elapsed_time;
	int paused = 0;
	int changed = 0;
	int cmd = 0;
	int param0 = 0;
	int param1 = 0;

	qr_total_width = 0;
	qr_offset_x = 0;
	qr_offset_y = 0;

	info = registered_fb[0];
	if (!info)
		printk("QR: Unable to get hand of a framebuffer!\n");

	tar_strategy_init();

	qr_oops_cmd = QR_OOPS_CMD_NOTHING;
	qr_oops_param0 = 0;
	qr_oops_param1 = 0;

	last_time = jiffies_to_usecs(get_jiffies_64());
	time_accumulator = 0;

	while (true) {
		msleep(100);

		time_now = jiffies_to_usecs(get_jiffies_64());
		elapsed_time = time_now - last_time;
		last_time = time_now;

		cmd = qr_oops_cmd;
		qr_oops_cmd = QR_OOPS_CMD_NOTHING;
		param0 = qr_oops_param0;
		param1 = qr_oops_param1;

		if (cmd != QR_OOPS_CMD_NOTHING) {
			qr_oops_param0 = 0;
			qr_oops_param1 = 0;
		}

		switch (cmd) {
		case QR_OOPS_CMD_NOTHING:
			break;
		case QR_OOPS_CMD_PRINT_MESSAGES:
			print_messages();
			break;
		case QR_OOPS_CMD_PRINT_PACKETS:
			if (param0)
				print_packets_by_msg(param0);
			else
				print_packets();
			break;
		case QR_OOPS_CMD_DELETE_MESSAGE:
			qr_list_delete_message(param0);
			tar_strategy_init();
			break;
		case QR_OOPS_CMD_DELETE_PACKET:
			qr_list_delete_packet(param0, param1);
			tar_strategy_init();
			break;
		case QR_OOPS_CMD_PAUSE:
			if (!paused) {
				paused = 1;
				clear_last_qr();
			}
			break;
		case QR_OOPS_CMD_RESUME:
			paused = 0;
			break;
		case QR_OOPS_CMD_CLEAR_QUEUE:
			qr_list_clear();
			tar_strategy_init();
			break;
		case QR_OOPS_CMD_STOP:
			/*
			 *  Not implemented
			 */
			break;
		default:
			printk("QR: invalid command: %d\n", qr_oops_cmd);
			break;
		}

		if (paused)
			continue;

		changed = 0;
		time_accumulator += elapsed_time;
		if (time_accumulator > QR_THREAD_TIME_STEP) {
			time_accumulator -= QR_THREAD_TIME_STEP;

			tar_strategy_next_step();
			if (curr_qr != tar_strategy_get_qrcode()) {
				curr_qr = tar_strategy_get_qrcode();
				changed = 1;
			}

			if(changed && info)
				printk("QR: force console flush\n");
		}

		if (!curr_qr) {
			if(changed)
				clear_last_qr();
			continue;
		}

		if (info) {
			console_lock();

			rect.width = qr_total_width;
			rect.height = qr_total_width;
			rect.dx = qr_offset_x;
			rect.dy = qr_offset_y;
			rect.rop = 0;
			rect.color = QQQ_BLACK;
			cfb_fillrect(info, &rect);

			w = compute_w(info, curr_qr->width);
			qr_total_width = (curr_qr->width + 2) * w;
			qr_offset_x = info->var.xres - qr_total_width;
			qr_offset_y = 0;

			draw_qr(info, curr_qr, qr_offset_x, qr_offset_y,
				w, w, 1);

			console_unlock();
		} else if (changed) {
			draw_ascii_qr(curr_qr);
		}
	}

	return 0;
}

int qr_thread_init(void)
{
	char qr_thread_name[]="qr_message_thread";
	qr_thread = kthread_create(qr_thread_func,NULL,qr_thread_name);
	if(qr_thread)
		wake_up_process(qr_thread);

	return 0;
}

void qr_thread_cleanup(void)
{
	int ret;
	ret = kthread_stop(qr_thread);
	if(!ret)
		printk(KERN_INFO "QR thread stopped");
}

void qr_append(char *text)
{
	size_t len;

	len = strlen(text);
	if (len + buf_pos >= MESSAGE_BUFSIZE - 1) {
		len = MESSAGE_BUFSIZE - 1 - buf_pos;
		qr_buffer[MESSAGE_BUFSIZE - 1] = '\0';
	}
	memcpy(&qr_buffer[buf_pos], text, len);
	buf_pos += len;
}

void print_qr_err(void)
{
	make_bk1_message();

	buf_pos = 0;

	if(!qr_thread)
		qr_thread_init();
}
