#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include "image.hh"

int ppm_init(image_ctx_t *ctx){
	size_t buf_size;

	/* Perform simple sanity checking */
	if(ctx->canvas != NULL)
		return EINVAL;
	ctx->model = IMAGE_RGB; //Override this as we only support RGB
	ctx->format = IMAGE_PPM;
	ctx->extension = (char*)".ppm";

	buf_size = ctx->width * ctx->height * RGB_SIZE;

	/* Allocate the canvas buffer */
	ctx->canvas = (uint8_t*) calloc(buf_size,sizeof(uint8_t));
	ctx->next_pixel = ctx->canvas;
	if(ctx->canvas == NULL)
		return ENOMEM;

	return 0;
}

int ppm_add_pixel(image_ctx_t *ctx,const uint8_t r,const uint8_t g,const uint8_t b,const uint8_t a){
	register uint8_t* next;

	next = ctx->next_pixel;

	next[0] = r;
	next[1] = g;
	next[2] = b;

	ctx->next_pixel += 3;

	return 0;
}

int ppm_get_pixel(image_ctx_t *ctx,const size_t x,const size_t y,const uint8_t *r,const uint8_t *g,const uint8_t *b,const uint8_t *a){
	/* STUB */
	return 0;
}

int ppm_write(image_ctx_t *ctx, FILE* fd){
	size_t written;
	size_t count;

	count = ctx->width * ctx->height * RGB_SIZE;

	fprintf(fd, "P6\n%zu %zu\n255\n", ctx->width, ctx->height);
	written = fwrite(ctx->canvas,sizeof(uint8_t),count,fd);
	if(written < count)
		return EPIPE;
	
	return 0;
}