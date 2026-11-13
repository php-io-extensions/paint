/*
 * What the class source shares with the module: the Zend object that holds a
 * surface, and its class entry.
 */
#ifndef PHPPAINT_RUNTIME_H
#define PHPPAINT_RUNTIME_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "php_paint.h"
#include "core.h"

typedef struct {
	paint_surface surface;   /* px is NULL until the surface is made */
	zend_string *pixels;     /* the surface's own bytes; NULL over memory it does not own */
	zval owner;              /* what the memory belongs to, kept alive; UNDEF when the surface owns its bytes */
	zend_object std;
} phppaint_surface;

static zend_always_inline phppaint_surface *phppaint_surface_from(zend_object *obj)
{
	return (phppaint_surface *) ((char *) obj - XtOffsetOf(phppaint_surface, std));
}

extern zend_class_entry *phppaint_ce_PaintSurface;

void phppaint_register_PaintSurface(void);

#endif
