/*
 * line.C - http://www.levork.org/l2rib.html
 *
 * Copyright © 2001-2004 by Julian Fong (http://www.levork.org/).  All
 * rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * The RenderMan (R) Interface Procedures and RIB Protocol are:
 * Copyright 1988, 1989, Pixar. All rights reserved.
 * RenderMan (R) is a registered trademark of Pixar.
 *
 * This is a procedural for use with PhotoRealistic RenderMan which
 * draws curves (linear or cubic) with a constant width in screen
 * space, primarily on behalf of l2rib. This solves the problem of
 * RiCurves' width being in object space, while allowing them to
 * inherit rotation and translations.
 */

#define LINEWIDTH 0.001

#include <math.h>
#include <ri.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
#define export __declspec(dllexport)
#else
#define export
#endif

#ifdef __cplusplus
extern "C" {
    export RtPointer ConvertParameters(RtString paramstr);
    export RtVoid Subdivide(RtPointer data, RtFloat detail);
    export RtVoid Free(RtPointer data);
}
#endif

enum lineType {
    LINEAR,
    LINEAR_OPTIONAL,
    CUBIC
};

struct pointData {
    int length;
    enum lineType type;
    RtPoint* points;
    RtPoint* testpoints; // For optional lines
};

export RtPointer ConvertParameters(RtString param)
{
    char* myparam = strdup(param);
    struct pointData* points;
    char* token;
    int i, type;

    points = (struct pointData *) calloc (1, sizeof(struct pointData));
    token = strtok(myparam, " ");
    points->length = atoi(token);
    token = strtok(NULL, " ");    
    type = atoi(token);
    if (type == 1) {
	points->type = LINEAR;
	points->points = (RtPoint*) malloc(points->length * 2 * sizeof(RtPoint));
	for (i = 0; i < points->length * 2; ++i) {
	    token = strtok(NULL, " ");
	    points->points[i][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][2] = atof(token);
	}
    } else if (type == 2) {
	points->type = LINEAR_OPTIONAL;
	points->points = (RtPoint*) malloc(points->length * 2 * sizeof(RtPoint));
	points->testpoints = (RtPoint*) malloc(points->length * 2 * sizeof(RtPoint));	
	for (i = 0; i < points->length * 2; i+=2) {
	    token = strtok(NULL, " ");
	    points->points[i][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][2] = atof(token);

	    token = strtok(NULL, " ");
	    points->points[i+1][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i+1][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i+1][2] = atof(token);

	    token = strtok(NULL, " ");
	    points->testpoints[i][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->testpoints[i][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->testpoints[i][2] = atof(token);

	    token = strtok(NULL, " ");
	    points->testpoints[i+1][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->testpoints[i+1][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->testpoints[i+1][2] = atof(token);
	}
    } else if (type == 3) {
	points->type = CUBIC;
	points->points = (RtPoint*) malloc(points->length * 4 * sizeof(RtPoint));
	for (i = 0; i < points->length * 4; ++i) {
	    token = strtok(NULL, " ");
	    points->points[i][0] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][1] = atof(token);
	    token = strtok(NULL, " ");
	    points->points[i][2] = atof(token);
	}
    }
    free(myparam);
    return (RtPointer) points;
}


export RtVoid Subdivide(RtPointer data, RtFloat detail)
{
    struct pointData *points = (struct pointData*) data;
    RtInt* nvertices;
    RtFloat* width;
    RtToken type;
    int i, start, end;

    nvertices = (RtInt*) malloc(points->length * sizeof(RtInt));
    /* Two widths per curve, since each curve is one segment */
    width = (RtFloat*) malloc(2 * (points->length) * sizeof(RtFloat));
    if (points->type == LINEAR || points->type == LINEAR_OPTIONAL) {
	type = RI_LINEAR;
	for (i = 0; i < points->length; ++i) {
	    nvertices[i] = 2;
	}
	RiTransformPoints("object", "world", points->length * 2, points->points);
    } else if (points->type == CUBIC) {
	type = RI_CUBIC;
	for (i = 0; i < points->length; ++i) {
	    nvertices[i] = 4;
	}
	RiTransformPoints("object", "world", points->length * 4, points->points);
	RiBasis(RiBezierBasis, RI_BEZIERSTEP, RiBezierBasis, RI_BEZIERSTEP);
    }

    /*
     * Computing line widths for each segment. Make a line cover
     * LINEWIDTH units of NDC space.  Project a disc in NDC to world
     * space and see how much width space units we need.
     */
    start = 0;
    if (points->type == LINEAR || points->type == LINEAR_OPTIONAL) {
	end = 1;
    } else {
	end = 3;
    }
    for (i = 0; i < points->length; ++i) {
	/* Line segment beginning width */
	RtPoint widthtest[2];
	float xdiff, ydiff, zdiff;
	
	memcpy(widthtest[0], points->points[start], sizeof(RtPoint));
	RiTransformPoints("world", "NDC", 1, widthtest);
	widthtest[1][0] = widthtest[0][0] + LINEWIDTH;
	widthtest[1][1] = widthtest[0][1] + LINEWIDTH;
	widthtest[1][2] = widthtest[0][2];
	RiTransformPoints("NDC", "world", 2, widthtest);
	xdiff = widthtest[1][0] - widthtest[0][0];
	ydiff = widthtest[1][1] - widthtest[0][1];
	zdiff = widthtest[1][2] - widthtest[0][2];
	width[2*i] = sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

	/* Line segment end width */
	memcpy(widthtest[0], points->points[end], sizeof(RtPoint));	
	RiTransformPoints("world", "NDC", 1, widthtest);
	widthtest[1][0] = widthtest[0][0] + LINEWIDTH;
	widthtest[1][1] = widthtest[0][1] + LINEWIDTH;
	widthtest[1][2] = widthtest[0][2];
	RiTransformPoints("NDC", "world", 2, widthtest);
	xdiff = widthtest[1][0] - widthtest[0][0];
	ydiff = widthtest[1][1] - widthtest[0][1];
	zdiff = widthtest[1][2] - widthtest[0][2];	
	width[2*i+1] = sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

	if (points->type == LINEAR || points->type == LINEAR_OPTIONAL) {
	    start += 2; end += 2;
	} else {
	    start += 4; end += 4;
	}
    }

    RiIdentity();

    if (points->type == LINEAR || points->type == CUBIC) {
	RiCurves(type, points->length, nvertices, "nonperiodic", "P", points->points, "width", width, RI_NULL);
    } else {
	/* Handling of optional; each will be a separate curve call */
	for (i = 0; i < points->length; i += 2) {
	    float a, b, c, sign1, sign2;
	    
	    RtPoint rastertest[4];

	    /* Transform all four points to raster space */
	    memcpy(rastertest[0], points->points[i], 2 * sizeof(RtPoint));
	    memcpy(rastertest[2], points->testpoints[i], 2 * sizeof(RtPoint));
	    RiTransformPoints("world", "raster", 4, rastertest);

	    /*
	     * Test to see which side of the line the two test points
	     * are.  We solve for the coefficient of the equation ax +
	     * by + c = 0 using the first two points, then put the two
	     * test points into that equation and compare the signs.
	     */
	    a = rastertest[0][1] - rastertest[1][1];
	    b = rastertest[1][0] - rastertest[0][0];
	    c = (rastertest[0][0] * rastertest[1][1] -
		 rastertest[1][0] * rastertest[0][1]);
	    
	    sign1 = a * rastertest[0][0] + b * rastertest[0][1] + c;
	    sign2 = a * rastertest[1][0] + b * rastertest[1][1] + c;
	    
	    sign1 = a * rastertest[2][0] + b * rastertest[2][1] + c;
	    sign2 = a * rastertest[3][0] + b * rastertest[3][1] + c;

	    if ((sign1 > 0) == (sign2 > 0)) {
		/* If test passed, emit the curve */
		RiCurves(type, 1, nvertices + i, "nonperiodic", "P", points->points + 2 * i, "width", width + i, RI_NULL);
	    }
	}

    }
    free(nvertices);
    free(width);
}

export RtVoid Free(RtPointer data)
{
    struct pointData *points = (struct pointData*) data;
    free(points->points);
    if (points->testpoints) {
	free(points->testpoints);
    }
    free(points);
}
