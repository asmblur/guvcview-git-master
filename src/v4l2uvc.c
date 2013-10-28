/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

/*******************************************************************************#
#                                                                               #
#  V4L2 interface                                                               #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <libv4l2.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <errno.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
/* support for internationalization - i18n */
#include <glib/gi18n.h>

#include "v4l2uvc.h"
#include "v4l2_dyna_ctrls.h"
#include "uvc_h264.h"
#include "utils.h"
#include "picture.h"
#include "colorspaces.h"
#include "ms_time.h"

#define __VMUTEX &videoIn->mutex

/* needed only for language files (not used)*/

// V4L2 control strings
#define CSTR_USER_CLASS		N_("User Controls")
#define	CSTR_BRIGHT 		N_("Brightness")
#define	CSTR_CONTRAST 		N_("Contrast")
#define	CSTR_HUE 		N_("Hue")
#define	CSTR_SATURAT		N_("Saturation")
#define	CSTR_SHARP		N_("Sharpness")
#define	CSTR_GAMMA		N_("Gamma")
#define	CSTR_BLCOMP		N_("Backlight Compensation")
#define	CSTR_PLFREQ		N_("Power Line Frequency")
#define CSTR_HUEAUTO		N_("Hue, Automatic")
#define	CSTR_FOCUSAUTO		N_("Focus, Auto")
#define CSTR_EXPMENU1		N_("Manual Mode")
#define CSTR_EXPMENU2		N_("Auto Mode")
#define CSTR_EXPMENU3		N_("Shutter Priority Mode")
#define CSTR_EXPMENU4		N_("Aperture Priority Mode")
#define CSTR_BLACK_LEVEL	N_("Black Level")
#define CSTR_AUTO_WB		N_("White Balance, Automatic")
#define CSTR_DO_WB		N_("Do White Balance")
#define CSTR_RB			N_("Red Balance")
#define	CSTR_BB			N_("Blue Balance")
#define CSTR_EXP		N_("Exposure")
#define CSTR_AUTOGAIN		N_("Gain, Automatic")
#define	CSTR_GAIN		N_("Gain")
#define CSTR_HFLIP		N_("Horizontal Flip")
#define CSTR_VFLIP		N_("Vertical Flip")
#define CSTR_HCENTER		N_("Horizontal Center")
#define CSTR_VCENTER		N_("Vertical Center")
#define CSTR_CHR_AGC		N_("Chroma AGC")
#define CSTR_CLR_KILL		N_("Color Killer")
#define CSTR_COLORFX		N_("Color Effects")

// CAMERA CLASS control strings
#define CSTR_CAMERA_CLASS	N_("Camera Controls")
#define CSTR_EXPAUTO		N_("Auto Exposure")
#define	CSTR_EXPABS		    N_("Exposure Time, Absolute")
#define CSTR_EXPAUTOPRI		N_("Exposure, Dynamic Framerate")
#define	CSTR_PAN_REL		N_("Pan, Relative")
#define CSTR_TILT_REL		N_("Tilt, Relative")
#define CSTR_PAN_RESET		N_("Pan, Reset")
#define CSTR_TILT_RESET		N_("Tilt, Reset")
#define CSTR_PAN_ABS		N_("Pan, Absolute")
#define CSTR_TILT_ABS		N_"Tilt, Absolute")
#define CSTR_FOCUS_ABS		N_("Focus, Absolute")
#define CSTR_FOCUS_REL		N_("Focus, Relative")
#define CSTR_FOCUS_AUTO		N_("Focus, Automatic")
#define CSTR_ZOOM_ABS		N_("Zoom, Absolute")
#define CSTR_ZOOM_REL		N_("Zoom, Relative")
#define CSTR_ZOOM_CONT		N_("Zoom, Continuous")
#define CSTR_PRIV		N_("Privacy")

//UVC specific control strings
#define	CSTR_EXPAUTO_UVC	N_("Exposure, Auto")
#define	CSTR_EXPAUTOPRI_UVC	N_("Exposure, Auto Priority")
#define	CSTR_EXPABS_UVC		N_("Exposure (Absolute)")
#define	CSTR_WBTAUTO_UVC	N_("White Balance Temperature, Auto")
#define	CSTR_WBT_UVC		N_("White Balance Temperature")
#define CSTR_WBCAUTO_UVC	N_("White Balance Component, Auto")
#define CSTR_WBCB_UVC		N_("White Balance Blue Component")
#define	CSTR_WBCR_UVC		N_("White Balance Red Component")

//libwebcam specific control strings
#define CSTR_FOCUS_LIBWC	N_("Focus")
#define CSTR_FOCUSABS_LIBWC	N_("Focus (Absolute)")


/* ioctl with a number of retries in the case of failure
* args:
* fd - device descriptor
* IOCTL_X - ioctl reference
* arg - pointer to ioctl data
* returns - ioctl result
*/
int xioctl(int fd, int IOCTL_X, void *arg)
{
	int ret = 0;
	int tries= IOCTL_RETRY;
	do
	{
		ret = v4l2_ioctl(fd, IOCTL_X, arg);
	}
	while (ret && tries-- &&
			((errno == EINTR) || (errno == EAGAIN) || (errno == ETIMEDOUT)));

	if (ret && (tries <= 0)) g_printerr("ioctl (%i) retried %i times - giving up: %s)\n", IOCTL_X, IOCTL_RETRY, strerror(errno));

	return (ret);
}

/* Query video device capabilities and supported formats
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 *
 * returns: error code  (0- OK)
*/
static int check_videoIn(struct vdIn *vd, struct GLOBAL *global)
{
	if (vd == NULL)
		return VDIN_ALLOC_ERR;

	memset(&vd->cap, 0, sizeof(struct v4l2_capability));

	if ( xioctl(vd->fd, VIDIOC_QUERYCAP, &vd->cap) < 0 )
	{
		perror("VIDIOC_QUERYCAP error");
		return VDIN_QUERYCAP_ERR;
	}

	if ( ( vd->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE ) == 0)
	{
		g_printerr("Error opening device %s: video capture not supported.\n",
				vd->videodevice);
		return VDIN_QUERYCAP_ERR;
	}
	if (!(vd->cap.capabilities & V4L2_CAP_STREAMING))
	{
		g_printerr("%s does not support streaming i/o\n",
			vd->videodevice);
		return VDIN_QUERYCAP_ERR;
	}

	if(vd->cap_meth == IO_READ)
	{

		vd->mem[vd->buf.index] = NULL;
		if (!(vd->cap.capabilities & V4L2_CAP_READWRITE))
		{
			g_printerr("%s does not support read i/o\n",
				vd->videodevice);
			return VDIN_READ_ERR;
		}
	}
	g_print("Init. %s (location: %s)\n", vd->cap.card, vd->cap.bus_info);

	vd->listFormats = enum_frame_formats( &global->width, &global->height, vd->fd);
	/*
	 * logitech c930 camera supports h264 through aux stream multiplexing in the MJPG container
	 * check if H264 UVCX XU h264 controls exist and add a virtual H264 format entry to the list
	 */
	check_uvc_h264_format(vd, global);

	if(!(vd->listFormats->listVidFormats))
		g_printerr("Couldn't detect any supported formats on your device (%i)\n", vd->listFormats->numb_formats);
	return VDIN_OK;
}

static int unmap_buff(struct vdIn *vd)
{
	int i=0;
	int ret=0;

	switch(vd->cap_meth)
	{
		case IO_READ:
			break;

		case IO_MMAP:
			for (i = 0; i < NB_BUFFER; i++)
			{
				// unmap old buffer
				if((vd->mem[i] != MAP_FAILED) && vd->buff_length[i])
					if((ret=v4l2_munmap(vd->mem[i], vd->buff_length[i]))<0)
					{
						perror("couldn't unmap buff");
					}
			}
	}
	return ret;
}

static int map_buff(struct vdIn *vd)
{
	int i = 0;
	// map new buffer
	for (i = 0; i < NB_BUFFER; i++)
	{
		vd->mem[i] = v4l2_mmap( NULL, // start anywhere
			vd->buff_length[i],
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			vd->fd,
			vd->buff_offset[i]);
		if (vd->mem[i] == MAP_FAILED)
		{
			perror("Unable to map buffer");
			return VDIN_MMAP_ERR;
		}
	}

	return (0);
}

/* Query and map buffers
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 * setUNMAP: ( flag )if set unmap old buffers first
 *
 * returns: error code  (0- OK)
*/
static int query_buff(struct vdIn *vd)
{
	int i=0;
	int ret=0;

	switch(vd->cap_meth)
	{
		case IO_READ:
			break;

		case IO_MMAP:
			for (i = 0; i < NB_BUFFER; i++)
			{
				memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
				vd->buf.index = i;
				vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				//vd->buf.flags = V4L2_BUF_FLAG_TIMECODE;
				//vd->buf.timecode = vd->timecode;
				//vd->buf.timestamp.tv_sec = 0;//get frame as soon as possible
				//vd->buf.timestamp.tv_usec = 0;
				vd->buf.memory = V4L2_MEMORY_MMAP;
				ret = xioctl(vd->fd, VIDIOC_QUERYBUF, &vd->buf);
				if (ret < 0)
				{
					perror("VIDIOC_QUERYBUF - Unable to query buffer");
					if(errno == EINVAL)
					{
						g_printerr("trying with read method instead\n");
						vd->cap_meth = IO_READ;
					}
					return VDIN_QUERYBUF_ERR;
				}
				if (vd->buf.length <= 0)
					g_printerr("WARNING VIDIOC_QUERYBUF - buffer length is %d\n",
						vd->buf.length);

				vd->buff_length[i] = vd->buf.length;
				vd->buff_offset[i] = vd->buf.m.offset;
			}
			// map the new buffers
			if(map_buff(vd) != 0)
				return VDIN_MMAP_ERR;
	}
	return VDIN_OK;
}

/* Queue Buffers
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 *
 * returns: error code  (0- OK)
*/
static int queue_buff(struct vdIn *vd)
{
	int i=0;
	int ret=0;
	switch(vd->cap_meth)
	{
		case IO_READ:
			break;

		case IO_MMAP:
		default:
			for (i = 0; i < NB_BUFFER; ++i)
			{
				memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
				vd->buf.index = i;
				vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				//vd->buf.flags = V4L2_BUF_FLAG_TIMECODE;
				//vd->buf.timecode = vd->timecode;
				//vd->buf.timestamp.tv_sec = 0;//get frame as soon as possible
				//vd->buf.timestamp.tv_usec = 0;
				vd->buf.memory = V4L2_MEMORY_MMAP;
				ret = xioctl(vd->fd, VIDIOC_QBUF, &vd->buf);
				if (ret < 0)
				{
					perror("VIDIOC_QBUF - Unable to queue buffer");
					return VDIN_QBUF_ERR;
				}
			}
			vd->buf.index = 0; /*reset index*/
	}
	return VDIN_OK;
}

/*
 * check buff (*buff) of size (size) for NALU type (type)
 * returns:
 *  buffer pointer to NALU type data if found
 *  NULL if not found
 */
static uint8_t* check_NALU(uint8_t type, uint8_t *buff, int size)
{
	uint8_t *sp = buff;
	uint8_t *nal = NULL;
	//search for NALU of type
	for(sp = buff; sp < buff + size - 5; ++sp)
	{
		if(sp[0] == 0x00 &&
		   sp[1] == 0x00 &&
		   sp[2] == 0x00 &&
		   sp[3] == 0x01 &&
		   (sp[4] & 0x1F) == type)
		{
			//found it
			nal = sp + 4;
			break;
		}
	}

	return nal;
}

/*
 * parses a buff (*buff) of size (size) for NALU type (type),
 * returns NALU size and sets pointer (NALU) to NALU data
 * returns -1 if no NALU found
 */
static int parse_NALU(uint8_t type, uint8_t **NALU, uint8_t *buff, int size)
{
	int nal_size = 0;
	uint8_t *sp = NULL;

	//search for NALU of type
	uint8_t *nal = check_NALU(type, buff, size);
	if(nal == NULL)
	{
		fprintf(stderr, "uvc H264: could not find NALU of type %i in buffer\n", type);
		return -1;
	}

	//search for end of NALU
	for(sp = nal; sp < buff + size - 4; ++sp)
	{
		if(sp[0] == 0x00 &&
		   sp[1] == 0x00 &&
		   sp[2] == 0x00 &&
		   sp[3] == 0x01)
		{
			nal_size = sp - nal;
			break;
		}
	}

	if(!nal_size)
		nal_size = buff + size - nal;

	*NALU = g_new0(uint8_t, nal_size);
	memcpy(*NALU, nal, nal_size);

	//char test_filename2[20];
	//snprintf(test_filename2, 20, "frame_nalu-%i.raw", type);
	//SaveBuff (test_filename2, nal_size, *NALU);

	return nal_size;
}

/*
 * demux a buff (*buff) of size (size) for NALU data,
 * returns data size and copies NALU data to h264 buffer
 */
static int demux_NALU(uint8_t *h264_data, uint8_t *buff, int size)
{
	uint8_t *sp = NULL;
	uint8_t *nal = buff;
	uint8_t *ph264 = h264_data;
	int nal_size = 0;
	int total_size = 0;
	int done = 0;

	while(!done)
	{
		//search for NALU start
		for(sp = nal; sp < buff + size - 4; ++sp)
		{
			if(sp[0] == 0x00 &&
			   sp[1] == 0x00 &&
			   sp[2] == 0x00 &&
			   sp[3] == 0x01)
			{
				nal = sp; //include NALU marker
				break;
			}
		}

		if(nal == buff)
		{
			fprintf(stderr, "uvc H264: could not find a NALU in buffer\n");
			return -1;
		}

		//search for next NALU (this marks the end of the previous)
		for(sp = nal+4; sp < buff + size - 4; ++sp)
		{
			if(sp[0] == 0x00 &&
			   sp[1] == 0x00 &&
			   sp[2] == 0x00 &&
			   sp[3] == 0x01)
			{
				nal_size = sp - nal;
				break;
			}
		}

		if(!nal_size)
		{
			nal_size = buff + size - nal;
			done = 1; //we reached the end of the buffer
		}

		//copy NALU to h264 data buffer
		memcpy(ph264, nal, nal_size);
		nal = sp; //reset to the next NALU marker
		ph264 += nal_size;
		total_size += nal_size;
		nal_size = 0;
	}

	//char test_filename2[20];
	//snprintf(test_filename2, 20, "frame.raw");
	//SaveBuff (test_filename2, total_size, h264_data);

	return total_size;
}

/*
 * Store the SPS and PPS of uvc H264 stream
 * if available in the frame
 */
static int store_extra_data(struct vdIn *vd)
{

	if(vd->h264_SPS == NULL)
	{
		vd->h264_SPS_size = parse_NALU( 7, &vd->h264_SPS, vd->h264_frame, vd->buf.bytesused);

		if(vd->h264_SPS_size <= 0 || vd->h264_SPS == NULL)
		{
			fprintf(stderr, "Could not find SPS (NALU type: 7)\n");
			return -1;
		}
		else
			printf("stored SPS %i bytes of data\n", vd->h264_SPS_size);
	}

	if(vd->h264_PPS == NULL)
	{
		vd->h264_PPS_size = parse_NALU( 8, &vd->h264_PPS, vd->h264_frame, vd->buf.bytesused);

		if(vd->h264_PPS_size <= 0 || vd->h264_PPS == NULL)
		{
			fprintf(stderr, "Could not find PPS (NALU type: 8)\n");
			return -1;
		}
		else
			printf("stored PPS %i bytes of data\n", vd->h264_PPS_size);
	}

	return 0;
}

/* check/store the last IDR frame
 * return:
 *  1 if IDR frame
 *  0 if non IDR frame
 */
static gboolean is_h264_keyframe (struct vdIn *vd)
{
	//check for a IDR frame type
	if(check_NALU(5, vd->h264_frame, vd->buf.bytesused) != NULL)
	{
		memcpy(vd->h264_last_IDR, vd->h264_frame, vd->buf.bytesused);
		vd->h264_last_IDR_size = vd->buf.bytesused;
		printf("IDR frame found in frame %" PRIu64 "\n", vd->frame_index);
		return 1;
	}

	return 0;
}

static void demux_h264(uint8_t* h264_data, uint8_t* frame_buffer, int bytesused)
{
	/*
	 * if it's a muxed stream we must demux it first
	 */
	if(get_SupPixFormatUvcH264() > 1)
	{
		demux_NALU(h264_data, frame_buffer, bytesused);
		return;
	}

	/*
	 * store the raw frame in h264 frame buffer
	 */
	memcpy(h264_data, frame_buffer, bytesused);

}

/* Enable video stream
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_STREAMON ioctl result (0- OK)
*/
int video_enable(struct vdIn *vd)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret=0;
	switch(vd->cap_meth)
	{
		case IO_READ:
			//do nothing
			break;

		case IO_MMAP:
		default:
			ret = xioctl(vd->fd, VIDIOC_STREAMON, &type);
			if (ret < 0)
			{
				perror("VIDIOC_STREAMON - Unable to start capture");
				return VDIN_STREAMON_ERR;
			}
			break;
	}
	vd->isstreaming = 1;
	return 0;
}

/* Disable video stream
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_STREAMOFF ioctl result (0- OK)
*/
int video_disable(struct vdIn *vd)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret=0;
	switch(vd->cap_meth)
	{
		case IO_READ:
			//do nothing
			break;

		case IO_MMAP:
		default:
			ret = xioctl(vd->fd, VIDIOC_STREAMOFF, &type);
			if (ret < 0)
			{
				perror("VIDIOC_STREAMOFF - Unable to stop capture");
				if(errno == 9) vd->isstreaming = 0;/*capture as allready stoped*/
				return VDIN_STREAMOFF_ERR;
			}
			break;
	}
	vd->isstreaming = 0;
	return 0;
}

/* gets video stream jpeg compression parameters
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_G_JPEGCOMP ioctl result value
*/
int get_jpegcomp(struct vdIn *vd)
{
	int ret = xioctl(vd->fd, VIDIOC_G_JPEGCOMP, &vd->jpgcomp);
	if(!ret)
	{
		g_print("VIDIOC_G_COMP:\n");
		g_print("    quality:      %i\n", vd->jpgcomp.quality);
		g_print("    APPn:         %i\n", vd->jpgcomp.APPn);
		g_print("    APP_len:      %i\n", vd->jpgcomp.APP_len);
		g_print("    APP_data:     %s\n", vd->jpgcomp.APP_data);
		g_print("    COM_len:      %i\n", vd->jpgcomp.COM_len);
		g_print("    COM_data:     %s\n", vd->jpgcomp.COM_data);
		g_print("    jpeg_markers: 0x%x\n", vd->jpgcomp.jpeg_markers);
	}
	else
	{
		perror("VIDIOC_G_COMP:");
		if(errno == EINVAL)
		{
			vd->jpgcomp.quality = -1; //not supported
			g_print("   compression control not supported\n");
		}
	}

	return (ret);
}

/* sets video stream jpeg compression parameters
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_S_JPEGCOMP ioctl result value
*/
int set_jpegcomp(struct vdIn *vd)
{
	int ret = xioctl(vd->fd, VIDIOC_S_JPEGCOMP, &vd->jpgcomp);
	if(ret != 0)
	{
		perror("VIDIOC_S_COMP:");
		if(errno == EINVAL)
		{
			vd->jpgcomp.quality = -1; //not supported
			g_print("   compression control not supported\n");
		}
	}

	return (ret);
}

/* Try/Set device video stream format
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 *
 * returns: error code ( 0 - VDIN_OK)
*/
static int init_v4l2(struct vdIn *vd, struct GLOBAL *global)//int *format, int *width, int *height, int *fps, int *fps_num)
{
	int ret = 0;

	// make sure we set a valid format
	g_print("checking format: %c%c%c%c\n",
		(global->format) & 0xFF, ((global->format) >> 8) & 0xFF,
		((global->format) >> 16) & 0xFF, ((global->format) >> 24) & 0xFF);

	if ((ret=check_SupPixFormat(global->format)) < 0)
	{
		// not available - Fail so we can check other formats (don't bother trying it)
		g_printerr("Format unavailable: %c%c%c%c\n",
			(global->format) & 0xFF, ((global->format) >> 8) & 0xFF,
			((global->format) >> 16) & 0xFF, ((global->format) >> 24) & 0xFF);
		return VDIN_FORMAT_ERR;
	}

	vd->timestamp = 0;
	// set format
	vd->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vd->fmt.fmt.pix.width = global->width;
	vd->fmt.fmt.pix.height = global->height;
	vd->fmt.fmt.pix.pixelformat = global->format;
	vd->fmt.fmt.pix.field = V4L2_FIELD_ANY;

	//if it's uvc muxed H264 we must use MJPG
	if(global->format == V4L2_PIX_FMT_H264 && get_SupPixFormatUvcH264() > 1)
		vd->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;


	ret = xioctl(vd->fd, VIDIOC_S_FMT, &vd->fmt);
	if (ret < 0)
	{
		perror("VIDIOC_S_FORMAT - Unable to set format");
		return VDIN_FORMAT_ERR;
	}
	if ((vd->fmt.fmt.pix.width != global->width) ||
		(vd->fmt.fmt.pix.height != global->height))
	{
		g_printerr("Requested Format unavailable: get width %d height %d \n",
		vd->fmt.fmt.pix.width, vd->fmt.fmt.pix.height);
		global->width = vd->fmt.fmt.pix.width;
		global->height = vd->fmt.fmt.pix.height;
	}

	/*
	 * if it's uvc muxed H264 we must now set UVCX_VIDEO_CONFIG_COMMIT
	 * with bStreamMuxOption =
	 */
	if(global->format == V4L2_PIX_FMT_H264 && get_SupPixFormatUvcH264() > 1)
	{
		set_muxed_h264_format(vd, global);
	}
	else
	{
		/* ----------- FPS --------------*/
		input_set_framerate(vd, &global->fps, &global->fps_num);
	}

	//deprecated in v4l2 - still waiting for new API implementation
	if(global->format == V4L2_PIX_FMT_MJPEG || global->format == V4L2_PIX_FMT_JPEG)
	{
		get_jpegcomp(vd);
	}

	switch (vd->cap_meth)
	{
		case IO_READ: //allocate buffer for read
			memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
			vd->buf.length = (global->width) * (global->height) * 3; //worst case (rgb)
			vd->mem[vd->buf.index] = g_new0(BYTE, vd->buf.length);
			break;

		case IO_MMAP:
		default:
			// request buffers
			memset(&vd->rb, 0, sizeof(struct v4l2_requestbuffers));
			vd->rb.count = NB_BUFFER;
			vd->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			vd->rb.memory = V4L2_MEMORY_MMAP;

			ret = xioctl(vd->fd, VIDIOC_REQBUFS, &vd->rb);
			if (ret < 0)
			{
				perror("VIDIOC_REQBUFS - Unable to allocate buffers");
				return VDIN_REQBUFS_ERR;
			}
			// map the buffers
			if (query_buff(vd))
			{
				//delete requested buffers
				//no need to unmap as mmap failed for sure
				memset(&vd->rb, 0, sizeof(struct v4l2_requestbuffers));
				vd->rb.count = 0;
				vd->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				vd->rb.memory = V4L2_MEMORY_MMAP;
				if(xioctl(vd->fd, VIDIOC_REQBUFS, &vd->rb)<0)
					perror("VIDIOC_REQBUFS - Unable to delete buffers");
				return VDIN_QUERYBUF_ERR;
			}
			// Queue the buffers
			if (queue_buff(vd))
			{
				//delete requested buffers
				unmap_buff(vd);
				memset(&vd->rb, 0, sizeof(struct v4l2_requestbuffers));
				vd->rb.count = 0;
				vd->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				vd->rb.memory = V4L2_MEMORY_MMAP;
				if(xioctl(vd->fd, VIDIOC_REQBUFS, &vd->rb)<0)
					perror("VIDIOC_REQBUFS - Unable to delete buffers");
				return VDIN_QBUF_ERR;
			}
	}

	return VDIN_OK;
}

/* Alloc image buffers for decoding video stream
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 *
 * returns: error code ( 0 - VDIN_OK)
*/
static int videoIn_frame_alloca(struct vdIn *vd, int format, int width, int height)
{
	int ret = VDIN_OK;
	size_t framebuf_size=0;
	size_t tmpbuf_size=0;

	int framesizeIn = (width * height << 1); //2 bytes per pixel
	switch (format)
	{
		case V4L2_PIX_FMT_H264:
			vd->h264_frame = g_new0(uint8_t, framesizeIn);
			vd->h264_last_IDR = g_new0(uint8_t, framesizeIn);
			vd->h264_last_IDR_size = 0; //reset (no frame stored)
			if(vd->h264_ctx)
				close_h264_decoder(vd->h264_ctx);
			vd->h264_ctx = init_h264_decoder(width, height); //init h264 context and fall through
		case V4L2_PIX_FMT_JPEG:
		case V4L2_PIX_FMT_MJPEG:
			// alloc a temp buffer to reconstruct the pict (MJPEG)
			tmpbuf_size= framesizeIn;
			vd->tmpbuffer = g_new0(uint8_t, tmpbuf_size);

			framebuf_size = width * (height + 8) * 2;
			vd->framebuffer = g_new0(uint8_t, framebuf_size);
			break;

		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_YYUV:
		case V4L2_PIX_FMT_YUV420: // only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel
		case V4L2_PIX_FMT_YVU420: // only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel
		case V4L2_PIX_FMT_Y41P:   // only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		case V4L2_PIX_FMT_SPCA501:
		case V4L2_PIX_FMT_SPCA505:
		case V4L2_PIX_FMT_SPCA508:
			// alloc a temp buffer for converting to YUYV
			tmpbuf_size= framesizeIn;
			vd->tmpbuffer = g_new0(unsigned char, tmpbuf_size);
			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;

		case V4L2_PIX_FMT_GREY:
			// alloc a temp buffer for converting to YUYV
			tmpbuf_size= width * height; // 1 byte per pixel
			vd->tmpbuffer = g_new0(unsigned char, tmpbuf_size);
			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;

	    case V4L2_PIX_FMT_Y10BPACK:
	    case V4L2_PIX_FMT_Y16:
			// alloc a temp buffer for converting to YUYV
			tmpbuf_size= width * height * 2; // 2 byte per pixel
			vd->tmpbuffer = g_new0(unsigned char, tmpbuf_size);
			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;

		case V4L2_PIX_FMT_YUYV:
			//  YUYV doesn't need a temp buffer but we will set it if/when
			//  video processing disable control is checked (bayer processing).
			//            (logitech cameras only)
			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;

		case V4L2_PIX_FMT_SGBRG8: //0
		case V4L2_PIX_FMT_SGRBG8: //1
		case V4L2_PIX_FMT_SBGGR8: //2
		case V4L2_PIX_FMT_SRGGB8: //3
			// Raw 8 bit bayer
			// when grabbing use:
			//    bayer_to_rgb24(bayer_data, RGB24_data, width, height, 0..3)
			//    rgb2yuyv(RGB24_data, vd->framebuffer, width, height)

			// alloc a temp buffer for converting to YUYV
			// rgb buffer for decoding bayer data
			tmpbuf_size = width * height * 3;
			vd->tmpbuffer = g_new0(unsigned char, tmpbuf_size);

			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_BGR24:
			//rgb or bgr (8-8-8)
			// alloc a temp buffer for converting to YUYV
			// rgb buffer
			tmpbuf_size = width * height * 3;
			vd->tmpbuffer = g_new0(unsigned char, tmpbuf_size);

			framebuf_size = framesizeIn;
			vd->framebuffer = g_new0(unsigned char, framebuf_size);
			break;

		default:
			g_printerr("(v4l2uvc.c) should never arrive (1)- exit fatal !!\n");
			ret = VDIN_UNKNOWN_ERR;
			if(vd->framebuffer)
				g_free(vd->framebuffer);
			vd->framebuffer = NULL;
			if(vd->tmpbuffer)
				g_free(vd->tmpbuffer);
			vd->tmpbuffer = NULL;
			return (ret);
	}

	if ((!vd->framebuffer) || (framebuf_size <=0))
		{
			g_printerr("couldn't calloc %lu bytes of memory for frame buffer\n",
				(unsigned long) framebuf_size);
			ret = VDIN_FBALLOC_ERR;
			if(vd->framebuffer)
				g_free(vd->framebuffer);
			vd->framebuffer = NULL;
			if(vd->tmpbuffer)
				g_free(vd->tmpbuffer);
			vd->tmpbuffer = NULL;
			return (ret);
		}
		else
		{
			int i = 0;
			// set framebuffer to black (y=0x00 u=0x80 v=0x80) by default
			for (i=0; i<(framebuf_size-4); i+=4)
				{
					vd->framebuffer[i]=0x00;  //Y
					vd->framebuffer[i+1]=0x80;//U
					vd->framebuffer[i+2]=0x00;//Y
					vd->framebuffer[i+3]=0x80;//V
				}
		}
	return (ret);
}

/* cleans VdIn struct and allocations
 * args:
 * pointer to initiated vdIn struct
 *
 * returns: void
*/
void clear_v4l2(struct vdIn *videoIn)
{
	v4l2_close(videoIn->fd);
	videoIn->fd=0;
	g_free(videoIn->videodevice);
	g_free(videoIn->VidFName);
	g_free(videoIn->ImageFName);
	close_h264_decoder(videoIn->h264_ctx);
	videoIn->h264_ctx = NULL;
	videoIn->videodevice = NULL;
	videoIn->VidFName = NULL;
	videoIn->ImageFName = NULL;
	videoIn->h264_last_IDR_size = 0;
	videoIn->h264_PPS_size = 0;
	videoIn->h264_SPS_size = 0;

	if(videoIn->cap_meth == IO_READ)
	{
		g_print("cleaning read buffer\n");
		if((videoIn->buf.length > 0) && videoIn->mem[0])
		{
			g_free(videoIn->mem[0]);
			videoIn->mem[0] = NULL;
		}
	}
	__CLOSE_MUTEX( __VMUTEX );
}

/* Init VdIn struct with default and/or global values
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 * global: pointer to a GLOBAL struct ( must be allready initiated )
 *
 * returns: error code ( 0 - VDIN_OK)
*/
int init_videoIn(struct vdIn *videoIn, struct GLOBAL *global)
{
	int ret = VDIN_OK;
	char *device = global->videodevice;

    /* Create a udev object */
    videoIn->udev = udev_new();

	__INIT_MUTEX( __VMUTEX );
	if (videoIn == NULL || device == NULL)
		return VDIN_ALLOC_ERR;
	if (global->width == 0 || global->height == 0)
		return VDIN_RESOL_ERR;
	if (global->cap_meth < IO_MMAP || global->cap_meth > IO_READ)
		global->cap_meth = IO_MMAP;		//mmap by default
	videoIn->cap_meth = global->cap_meth;
	if(global->debug) g_print("capture method = %i\n",videoIn->cap_meth);
	videoIn->videodevice = NULL;
	videoIn->videodevice = g_strdup(device);
	g_print("video device: %s \n", videoIn->videodevice);

	//flag to video thread
	videoIn->capVid = FALSE;
	//flag from video thread
	videoIn->VidCapStop=TRUE;

	videoIn->VidFName = g_strdup(global->vidFPath[0]);
	videoIn->signalquit = FALSE;
	videoIn->PanTilt=0;
	videoIn->isbayer = 0; //bayer mode off
	videoIn->pix_order=0; // pix order for bayer mode def: gbgbgb..|rgrgrg..
	videoIn->setFPS=0;
	videoIn->capImage=FALSE;
	videoIn->cap_raw=0;

	videoIn->ImageFName = g_strdup(global->imgFPath[0]);

	videoIn->h264_ctx = NULL;
	videoIn->h264_SPS = NULL;
	videoIn->h264_SPS_size = 0;
	videoIn->h264_PPS = NULL;
	videoIn->h264_PPS_size = 0;
	videoIn->h264_last_IDR = NULL;
	videoIn->h264_last_IDR_size = 0;
	//timestamps not supported by UVC driver
	//vd->timecode.type = V4L2_TC_TYPE_25FPS;
	//vd->timecode.flags = V4L2_TC_FLAG_DROPFRAME;

	videoIn->available_exp[0]=-1;
	videoIn->available_exp[1]=-1;
	videoIn->available_exp[2]=-1;
	videoIn->available_exp[3]=-1;

	videoIn->tmpbuffer = NULL;
	videoIn->framebuffer = NULL;
	videoIn->h264_frame = NULL;

    /*start udev device monitoring*/
    /* Set up a monitor to monitor v4l2 devices */
    if(videoIn->udev)
    {
        videoIn->udev_mon = udev_monitor_new_from_netlink(videoIn->udev, "udev");
        udev_monitor_filter_add_match_subsystem_devtype(videoIn->udev_mon, "video4linux", NULL);
        udev_monitor_enable_receiving(videoIn->udev_mon);
        /* Get the file descriptor (fd) for the monitor */
        videoIn->udev_fd = udev_monitor_get_fd(videoIn->udev_mon);
    }

    videoIn->listDevices = enum_devices( videoIn->videodevice, videoIn->udev, (int) global->debug);

	if (videoIn->listDevices != NULL)
	{
		if(!(videoIn->listDevices->listVidDevices))
			g_printerr("unable to detect video devices on your system (%i)\n", videoIn->listDevices->num_devices);
	}
	else
		g_printerr("Unable to detect devices on your system\n");

	if (videoIn->fd <=0 ) //open device
	{
		if ((videoIn->fd = v4l2_open(videoIn->videodevice, O_RDWR | O_NONBLOCK, 0)) < 0)
		{
			perror("ERROR opening V4L interface");
			ret = VDIN_DEVICE_ERR;
			clear_v4l2(videoIn);
			return (ret);
		}
	}

	//reset v4l2_format
	memset(&videoIn->fmt, 0, sizeof(struct v4l2_format));
	// populate video capabilities structure array
	// should only be called after all vdIn struct elements
	// have been initialized
	if((ret = check_videoIn(videoIn, global)) != VDIN_OK)
	{
		clear_v4l2(videoIn);
		return (ret);
	}

	//if it's a uvc device
	//map dynamic controls
	//and check for h264 support
	if(videoIn->listDevices->num_devices > 0)
	{
		g_print("vid:%04x \npid:%04x \ndriver:%s\n",
			videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].vendor,
			videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].product,
			videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].driver);
		if(g_strcmp0(videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].driver,"uvcvideo") == 0)
		{
			//check for uvc H264 support in the device
			uint64_t busnum = videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].busnum;
			uint64_t devnum = videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].devnum;
			uint8_t unit_id = xu_get_unit_id (busnum, devnum);

			if(has_h264_support(videoIn->fd, unit_id))
			{
				global->uvc_h264_unit = unit_id;
				videoIn->uvc_h264_unit = unit_id;
			}
			if(videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].vendor != 0)
			{
				//check for logitech vid
				if (videoIn->listDevices->listVidDevices[videoIn->listDevices->current_device].vendor == 0x046d)
					(ret=initDynCtrls(videoIn->fd));
				else ret= VDIN_DYNCTRL_ERR;
			}
			else (ret=initDynCtrls(videoIn->fd));
		}
		else ret = VDIN_DYNCTRL_ERR;

	}

	if(global->add_ctrls)
	{
		//added extension controls so now we can exit
		//set a return code for enabling the correct warning window
		ret = (ret ? VDIN_DYNCTRL_ERR: VDIN_DYNCTRL_OK);
		clear_v4l2(videoIn);
		return (ret);
	}
	else ret = 0; //clean ret code

	if(!(global->control_only))
	{
		if ((ret=init_v4l2(videoIn, global)) < 0)
		{
			g_printerr("Init v4L2 failed !! \n");
			clear_v4l2(videoIn);
			return (ret);
		}

		g_print("fps is set to %i/%i\n", global->fps_num, global->fps);
		/*allocations*/
		if((ret = videoIn_frame_alloca(videoIn, global->format, global->width, global->height)) != VDIN_OK)
		{
			clear_v4l2(videoIn);
			return (ret);
		}
	}
	return (ret);
}

/* decode video stream (frame buffer in yuyv format)
 * args:
 * vd: pointer to a VdIn struct ( must be allready allocated )
 *
 * returns: error code ( 0 - VDIN_OK)
*/
static int frame_decode(struct vdIn *vd, int format, int width, int height)
{
	int ret = VDIN_OK;
	int framesizeIn =(width * height << 1);//2 bytes per pixel
	switch (format)
	{
		case V4L2_PIX_FMT_H264:
			/*
			 * get the h264 frame
			 */
			demux_h264(vd->h264_frame, vd->mem[vd->buf.index], vd->buf.bytesused);

			/*
			 * store SPS and PPS info (usually the first two NALU)
			 * and check/store the last IDR frame
			 */
			store_extra_data(vd);

			/*
			 * check for keyframe
			 */
			vd->isKeyframe = is_h264_keyframe(vd);

			//decode if we already have a IDR frame
			if(vd->h264_last_IDR_size > 0)
			{
				/* decode (h264) to vd->tmpbuffer (yuv420p)*/
				decode_h264(vd->tmpbuffer, vd->mem[vd->buf.index], vd->buf.bytesused, vd->h264_ctx);
				yuv420_to_yuyv (vd->framebuffer, vd->tmpbuffer, width, height);
			}
			break;

		case V4L2_PIX_FMT_JPEG:
		case V4L2_PIX_FMT_MJPEG:
			if(vd->buf.bytesused <= HEADERFRAME1)
			{
				// Prevent crash on empty image
				g_print("Ignoring empty buffer ...\n");
				return (ret);
			}
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);

			if (jpeg_decode(&vd->framebuffer, vd->tmpbuffer, width, height) < 0)
			{
				g_printerr("jpeg decode errors\n");
				ret = VDIN_DECODE_ERR;
				return ret;
			}
			break;

		case V4L2_PIX_FMT_UYVY:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			uyvy_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_YVYU:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			yvyu_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_YYUV:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			yyuv_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_YUV420:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			yuv420_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_YVU420:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			yvu420_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_NV12:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			nv12_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_NV21:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			nv21_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_NV16:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			nv16_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_NV61:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			nv61_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_Y41P:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			y41p_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_GREY:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			grey_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_Y10BPACK:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			y10b_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

	    case V4L2_PIX_FMT_Y16:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			y16_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_SPCA501:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			s501_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_SPCA505:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			s505_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_SPCA508:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			s508_to_yuyv(vd->framebuffer, vd->tmpbuffer, width, height);
			break;

		case V4L2_PIX_FMT_YUYV:
			if(vd->isbayer>0)
			{
				if (!(vd->tmpbuffer))
				{
					// rgb buffer for decoding bayer data
					vd->tmpbuffer = g_new0(unsigned char,
						width * height * 3);
				}
				bayer_to_rgb24 (vd->mem[vd->buf.index],vd->tmpbuffer, width, height, vd->pix_order);
				// raw bayer is only available in logitech cameras in yuyv mode
				rgb2yuyv (vd->tmpbuffer,vd->framebuffer, width, height);
			}
			else
			{
				if (vd->buf.bytesused > framesizeIn)
					memcpy(vd->framebuffer, vd->mem[vd->buf.index],
						(size_t) framesizeIn);
				else
					memcpy(vd->framebuffer, vd->mem[vd->buf.index],
						(size_t) vd->buf.bytesused);
			}
			break;

		case V4L2_PIX_FMT_SGBRG8: //0
			bayer_to_rgb24 (vd->mem[vd->buf.index],vd->tmpbuffer, width, height, 0);
			rgb2yuyv (vd->tmpbuffer, vd->framebuffer, width, height);
			break;

		case V4L2_PIX_FMT_SGRBG8: //1
			bayer_to_rgb24 (vd->mem[vd->buf.index], vd->tmpbuffer, width, height, 1);
			rgb2yuyv (vd->tmpbuffer, vd->framebuffer, width, height);
			break;

		case V4L2_PIX_FMT_SBGGR8: //2
			bayer_to_rgb24 (vd->mem[vd->buf.index], vd->tmpbuffer, width, height, 2);
			rgb2yuyv (vd->tmpbuffer, vd->framebuffer, width, height);
			break;
		case V4L2_PIX_FMT_SRGGB8: //3
			bayer_to_rgb24 (vd->mem[vd->buf.index], vd->tmpbuffer, width, height, 3);
			rgb2yuyv (vd->tmpbuffer, vd->framebuffer, width, height);
			break;

		case V4L2_PIX_FMT_RGB24:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index], vd->buf.bytesused);
			rgb2yuyv(vd->tmpbuffer, vd->framebuffer, width, height);
			break;
		case V4L2_PIX_FMT_BGR24:
			memcpy(vd->tmpbuffer, vd->mem[vd->buf.index],vd->buf.bytesused);
			bgr2yuyv(vd->tmpbuffer, vd->framebuffer, width, height);
			break;

		default:
			g_printerr("error grabbing (v4l2uvc.c) unknown format: %i\n", format);
			ret = VDIN_UNKNOWN_ERR;
			return ret;
	}
	return ret;
}

static int check_frame_available(struct vdIn *vd)
{
	int ret = VDIN_OK;
	fd_set rdset;
	struct timeval timeout;
	//make sure streaming is on
	if (!vd->isstreaming)
		if (video_enable(vd))
		{
			vd->signalquit = TRUE;
			return VDIN_STREAMON_ERR;
		}

	FD_ZERO(&rdset);
	FD_SET(vd->fd, &rdset);
	timeout.tv_sec = 1; // 1 sec timeout
	timeout.tv_usec = 0;
	// select - wait for data or timeout
	ret = select(vd->fd + 1, &rdset, NULL, NULL, &timeout);
	if (ret < 0)
	{
		perror(" Could not grab image (select error)");
		vd->timestamp = 0;
		return VDIN_SELEFAIL_ERR;
	}
	else if (ret == 0)
	{
		perror(" Could not grab image (select timeout)");
		vd->timestamp = 0;
		return VDIN_SELETIMEOUT_ERR;
	}
	else if ((ret > 0) && (FD_ISSET(vd->fd, &rdset)))
		return VDIN_OK;
	else
		return VDIN_UNKNOWN_ERR;

}

/* Grabs video frame and decodes it if necessary
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: error code ( 0 - VDIN_OK)
*/
int uvcGrab(struct vdIn *vd, struct GLOBAL *global, int format, int width, int height)
{
	//request a IDR frame with SPS and PPS data if it's the first frame
	if(global->format == V4L2_PIX_FMT_H264 && vd->frame_index < 1)
		uvcx_request_frame_type(vd->fd, global->uvc_h264_unit, PICTURE_TYPE_IDR_FULL);

	int ret = check_frame_available(vd);

	UINT64 ts = 0;

	if (ret < 0)
		return ret;

	switch(vd->cap_meth)
	{
		case IO_READ:
			if(vd->setFPS > 0)
			{
				video_disable(vd);
				input_set_framerate (vd, &global->fps, &global->fps_num);
				video_enable(vd);
				vd->setFPS = 0; /*no need to query and queue buffers*/
			}
			vd->buf.bytesused = v4l2_read (vd->fd, vd->mem[vd->buf.index], vd->buf.length);
			vd->timestamp = ns_time_monotonic();
			if (-1 == vd->buf.bytesused )
			{
				switch (errno)
				{
					case EAGAIN:
						g_print("No data available for read\n");
						return VDIN_SELETIMEOUT_ERR;
						break;
					case EINVAL:
						perror("Read method error, try mmap instead");
						return VDIN_READ_ERR;
						break;
					case EIO:
						perror("read I/O Error");
						return VDIN_READ_ERR;
						break;
					default:
						perror("read");
						return VDIN_READ_ERR;
						break;
				}
				vd->timestamp = 0;
			}
			break;

		case IO_MMAP:
		default:
			/*query and queue buffers since fps or compression as changed*/
			if((vd->setFPS > 0) || (vd->setJPEGCOMP > 0))
			{
				/*------------------------------------------*/
				/*  change video fps or frame compression   */
				/*------------------------------------------*/
				if(vd->setFPS) //change fps
				{
					/*
					 * For uvc muxed H264 stream
					 * don't restart the video stream or codec values will be reset
					 */
					if(global->format == V4L2_PIX_FMT_H264 && get_SupPixFormatUvcH264() > 1)
					{
						uint32_t frame_interval = (global->fps_num * 1000000000LL / global->fps)/100;
						uvcx_set_frame_rate_config(vd->fd, global->uvc_h264_unit, frame_interval);
					}
					else
					{
						video_disable(vd);
						unmap_buff(vd);
						input_set_framerate (vd, &global->fps, &global->fps_num);
						vd->setFPS = 0;
						query_buff(vd);
						queue_buff(vd);
						video_enable(vd);
					}
				}
				else if(vd->setJPEGCOMP) //change jpeg quality/compression in video frame
				{
					video_disable(vd);
					unmap_buff(vd);
					set_jpegcomp(vd);
					get_jpegcomp(vd);
					query_buff(vd);
					queue_buff(vd);
					video_enable(vd);
					vd->setJPEGCOMP = 0;
				}

				ret = check_frame_available(vd);

				if (ret < 0)
					return ret;
			}

			/* dequeue the buffers */
			memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
			vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			vd->buf.memory = V4L2_MEMORY_MMAP;

			ret = xioctl(vd->fd, VIDIOC_DQBUF, &vd->buf);
			if (ret < 0)
			{
				perror("VIDIOC_DQBUF - Unable to dequeue buffer ");
				ret = VDIN_DEQBUFS_ERR;
				return ret;
			}
			ts = (UINT64) vd->buf.timestamp.tv_sec * G_NSEC_PER_SEC +
			    vd->buf.timestamp.tv_usec * 1000; //in nanosec
				/* use buffer timestamp if set by the driver, otherwise use current system time */
			if(ts > 0) vd->timestamp = ts;
			else vd->timestamp = ns_time_monotonic();

			ret = xioctl(vd->fd, VIDIOC_QBUF, &vd->buf);
			if (ret < 0)
			{
				perror("VIDIOC_QBUF - Unable to queue buffer");
				ret = VDIN_QBUF_ERR;
				return ret;
			}
	}


	// save raw frame
	if (vd->cap_raw > 0)
	{
		SaveBuff (vd->ImageFName,vd->buf.bytesused,vd->mem[vd->buf.index]);
		vd->cap_raw=0;
	}

	vd->frame_index++;

	//char test_filename[20];
	//snprintf(test_filename, 20, "frame-%i.raw", vd->frame_index);
	//vd->frame_index++;
	//SaveBuff (test_filename,vd->buf.bytesused,vd->mem[vd->buf.index]);

	if ((ret = frame_decode(vd, format, width, height)) != VDIN_OK)
	{
		vd->signalquit = TRUE;
		return ret;
	}

	return VDIN_OK;
}

static int close_v4l2_buffers (struct vdIn *vd)
{
	//clean frame buffers
	if(vd->tmpbuffer != NULL) g_free(vd->tmpbuffer);
	vd->tmpbuffer = NULL;
	if(vd->framebuffer != NULL) g_free(vd->framebuffer);
	vd->framebuffer = NULL;
	if(vd->h264_last_IDR != NULL) g_free(vd->h264_last_IDR);
	vd->h264_last_IDR = NULL;
	if(vd->h264_frame != NULL) g_free(vd->h264_frame);
	vd->h264_frame = NULL;
	//clean h264 SPS and PPS data buffers
	if(vd->h264_SPS != NULL) g_free(vd->h264_SPS);
	vd->h264_SPS  = NULL;
	if(vd->h264_PPS != NULL) g_free(vd->h264_PPS);
	vd->h264_PPS = NULL;
	// clean h264 decoder context
	close_h264_decoder(vd->h264_ctx);
	vd->h264_ctx = NULL;
	// unmap queue buffers
	switch(vd->cap_meth)
	{
		case IO_READ:
			if(vd->mem[vd->buf.index]!= NULL)
	    		{
				g_free(vd->mem[vd->buf.index]);
				vd->mem[vd->buf.index] = NULL;
			 }
			break;

		case IO_MMAP:
		default:
			//delete requested buffers
			unmap_buff(vd);
			memset(&vd->rb, 0, sizeof(struct v4l2_requestbuffers));
			vd->rb.count = 0;
			vd->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			vd->rb.memory = V4L2_MEMORY_MMAP;
			if(xioctl(vd->fd, VIDIOC_REQBUFS, &vd->rb)<0)
			{
				g_printerr("VIDIOC_REQBUFS - Failed to delete buffers: %s (errno %d)\n", strerror(errno), errno);
				return(VDIN_REQBUFS_ERR);
			}
			break;
	}
	return (VDIN_OK);
}


int restart_v4l2(struct vdIn *vd, struct GLOBAL *global)
{
	int ret = VDIN_OK;
	video_disable(vd);
	close_v4l2_buffers(vd);

	if ((ret=init_v4l2(vd, global)) < 0)
	{
		g_printerr("Init v4L2 failed !! \n");
		vd->signalquit = TRUE;
		return ret;
	}
	/*allocations*/
	if((ret = videoIn_frame_alloca(vd, global->format, global->width, global->height)) != VDIN_OK)
	{
		vd->signalquit = TRUE;
		return ret;
	}
	/*try to start the video stream*/
	//it's OK if it fails since it is retried in uvcGrab
	video_enable(vd);

	return (ret);
}

/* cleans VdIn struct and allocations
 * args:
 * pointer to initiated vdIn struct
 *
 * returns: void
*/
void close_v4l2(struct vdIn *videoIn, gboolean control_only)
{
	if (videoIn->isstreaming) video_disable(videoIn);

    if (videoIn->udev) udev_unref(videoIn->udev);

	if(videoIn->videodevice) g_free(videoIn->videodevice);
	if(videoIn->ImageFName)g_free(videoIn->ImageFName);
	if(videoIn->VidFName)g_free(videoIn->VidFName);
	// free format allocations
	if(videoIn->listFormats) freeFormats(videoIn->listFormats);
	if (!control_only)
	{
		close_v4l2_buffers(videoIn);
	}
	videoIn->h264_last_IDR = NULL;
	videoIn->h264_ctx = NULL;
	videoIn->videodevice = NULL;
	videoIn->tmpbuffer = NULL;
	videoIn->framebuffer = NULL;
	videoIn->h264_frame = NULL;
	videoIn->ImageFName = NULL;
	videoIn->VidFName = NULL;
	if(videoIn->listDevices != NULL) freeDevices(videoIn->listDevices);
	// close device descriptor
	if(videoIn->fd) v4l2_close(videoIn->fd);
	__CLOSE_MUTEX( __VMUTEX );
	// free struct allocation
	if(videoIn) g_free(videoIn);
	videoIn=NULL;
}

/* sets video device frame rate
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_S_PARM ioctl result value
*/
int
input_set_framerate (struct vdIn * device, int *fps, int *fps_num)
{
	int fd;
	int ret=0;



	fd = device->fd;

	device->streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = xioctl(fd, VIDIOC_G_PARM, &device->streamparm);
	if (ret < 0)
		return ret;

	if (!(device->streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME))
		return -ENOTSUP;

	device->streamparm.parm.capture.timeperframe.numerator = *fps_num;
	device->streamparm.parm.capture.timeperframe.denominator = *fps;

	ret = xioctl(fd,VIDIOC_S_PARM,&device->streamparm);
	if (ret < 0)
	{
		g_printerr("Unable to set %d/%d fps\n", *fps_num, *fps);
		perror("VIDIOC_S_PARM error");
	}

	/*make sure we now have the correct fps*/
	input_get_framerate (device, fps, fps_num);

	return ret;
}

/* gets video device defined frame rate (not real - consider it a maximum value)
 * args:
 * vd: pointer to a VdIn struct ( must be allready initiated)
 *
 * returns: VIDIOC_G_PARM ioctl result value
*/
int
input_get_framerate (struct vdIn * device, int *fps, int *fps_num)
{
	int fd;
	int ret=0;

	fd = device->fd;

	device->streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = xioctl(fd,VIDIOC_G_PARM,&device->streamparm);
	if (ret < 0)
	{
		perror("VIDIOC_G_PARM - Unable to get timeperframe");
	}
	else
	{
		if (device->streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
			// it seems numerator is allways 1 but we don't do assumptions here :-)
			*fps = device->streamparm.parm.capture.timeperframe.denominator;
			*fps_num = device->streamparm.parm.capture.timeperframe.numerator;
		}
	}

	if(*fps == 0 )
		*fps = 1;
	if(*fps_num == 0)
		*fps_num = 1;

	return ret;
}
