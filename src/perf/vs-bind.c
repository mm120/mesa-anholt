/**
 * Test states change when using shaders & textures.
 *
 * Copyright (C) 2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 2011  Red Hat      All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glew.h>
#include "glut_wrap.h"
#include "readtex.h"
#include "shaderutil.h"
#include "glmain.h"
#include "common.h"

static GLuint prog1;
static GLuint prog2;

int WinWidth = 500, WinHeight = 500;

static void
Draw(unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		glUseProgram(prog1);
		glDrawArrays(GL_POINTS, 0, 1);
		glUseProgram(prog2);
		glDrawArrays(GL_POINTS, 0, 1);
	}
	glutSwapBuffers();
}

void
PerfDraw(void)
{
   double rate;

   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   perf_printf("GLSL texture/program change rate\n");

   rate = PerfMeasureRate(Draw);
   perf_printf("  Immediate mode: %s change/sec\n", PerfHumanFloat(rate));

   exit(0);
}

void
PerfNextRound(void)
{
}

static void
bind_vbo(void)
{
	float point_pos[4] = {0.0, 0.0, 0.0, 0.0};
	GLuint vbo;

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER_ARB, vbo);
	glBufferData(GL_ARRAY_BUFFER_ARB, sizeof(point_pos), point_pos,
		     GL_STATIC_DRAW_ARB);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);
}

static GLuint
create_program(bool use_vs_1)
{
	const char *vs_0 =
		"attribute vec4 vert;\n"
		"varying vec4 color;\n"
		"void main() {"
		"	gl_Position = vert;\n"
		"	color = vec4(0.0, 1.0, 0.0, 0.0);\n"
		"}\n";
	const char *vs_1 =
		"attribute vec4 vert;\n"
		"varying vec4 color;\n"
		"void main() {"
		"	gl_Position = vert;\n"
		"	color = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"}\n";
	const char *fs_source =
		"varying vec4 color;\n"
		"void main() {"
		"	gl_FragColor = color;\n"
		"}\n";
	GLuint fs, vs, prog;

	vs = CompileShaderText(GL_VERTEX_SHADER, use_vs_1 ? vs_1 : vs_0);
	fs = CompileShaderText(GL_FRAGMENT_SHADER, fs_source);
	assert(vs);
	assert(fs);
	prog = LinkShaders(vs, fs);
	assert(prog);

	glUseProgram(prog);

	assert(ValidateShaderProgram(prog));

	assert(glGetAttribLocation(prog, "vert") == 0);

	return prog;
}

void
PerfInit(void)
{
	if (!ShadersSupported())
		exit(1);

	prog1 = create_program(false);
	prog2 = create_program(true);
	bind_vbo();

	glEnable(GL_DEPTH_TEST);

	glClearColor(.6, .6, .9, 0);
	glColor3f(1.0, 1.0, 1.0);
}
