#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <GL/glew.h>
#include "glut_wrap.h"
#include "readtex.h"
#include "shaderutil.h"


static GLint WinWidth = 1, WinHeight = 1;
static GLuint fragShader;
static GLuint vertShader;
static GLuint program;
static GLint win = 0;
static GLboolean Anim = GL_TRUE;


static void
Idle(void)
{
	glutPostRedisplay();
}


static void
Redisplay(void)
{
	float vcoord[2] = {0, 0};
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vcoord);

	glDrawArrays(GL_POINTS, 0, 1);


	glutSwapBuffers();

	glutDestroyWindow(win);
	exit(0);
}

static void
CleanUp(void)
{
	glDeleteShader(fragShader);
	glDeleteShader(vertShader);
	glDeleteProgram(program);
	glutDestroyWindow(win);
}


static void
Key(unsigned char key, int x, int y)
{
	(void) x;
	(void) y;

	switch(key) {
	case ' ':
	case 'a':
		Anim = !Anim;
		glutIdleFunc(Anim ? Idle : NULL);
		break;
	case 27:
		CleanUp();
		exit(0);
		break;
	}
	glutPostRedisplay();
}

static const char *TexFiles[1] =
   {
      DEMOS_DATA_DIR "tile.rgb",
   };

static void
InitTextures(void)
{
   GLenum filter = GL_LINEAR;
   int i = 0;

   GLint imgWidth, imgHeight;
   GLenum imgFormat;
   GLubyte *image = NULL;

   image = LoadRGBImage(TexFiles[i], &imgWidth, &imgHeight, &imgFormat);
   if (!image) {
	   printf("Couldn't read %s\n", TexFiles[i]);
	   exit(0);
   }

   glActiveTexture(GL_TEXTURE0 + i);
   glBindTexture(GL_TEXTURE_2D, 42 + i);
   gluBuild2DMipmaps(GL_TEXTURE_2D, 4, imgWidth, imgHeight,
		     imgFormat, GL_UNSIGNED_BYTE, image);
   free(image);
      
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

static void
Init(void)
{
	static const char *fragShaderText =
		"#version 130\n"
		"uniform float f[8];\n"
		"uniform sampler2D s;\n"
		""
		"void main() { \n"
		"   float a = 0;\n"

		"   a = textureSize(s, 0).x;\n"
		"   a += textureSize(s, 0).x;\n"

		/*
		"   for (int i = 0; i < f[1] + 0; i++) {\n"
		"      a += texture2D(s, vec2(0, 0)).x;\n"
		"      a *= 2.0;\n"
		"   }\n"
		*/

		/*
		"   float a = 0.5 * f[0];\n"
		"   if (bool(f[0])) { a = 0.25 * f[0]; }\n"
		*/

		"//float a = f[0] * f[1];\n"

		"   //float b = texture2D(s, vec2(0, 0)).x;\n"
		"   //b = b + 0.5;\n"

		"   float b = f[1];\n"
		"   gl_FragColor = vec4(f[0], a, b, f[7]); \n"
		"}\n";
	static const char *vertShaderText =
		"#version 130\n"
		"void main() {\n"
		"   gl_Position = gl_Vertex * vec4(1.0001);\n"
		"}\n";

	if (!ShadersSupported())
		exit(1);

	vertShader = CompileShaderText(GL_VERTEX_SHADER, vertShaderText);
	fragShader = CompileShaderText(GL_FRAGMENT_SHADER, fragShaderText);
	program = LinkShaders(vertShader, fragShader);

	glUseProgram(program);

	/*assert(glGetError() == 0);*/

	assert(glIsProgram(program));
	assert(glIsShader(fragShader));
	assert(glIsShader(vertShader));

	InitTextures();
}

int
main(int argc, char *argv[])
{
	glutInit(&argc, argv);
	glutInitWindowSize(WinWidth, WinHeight);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	win = glutCreateWindow(argv[0]);
	glewInit();
	glutKeyboardFunc(Key);
	glutDisplayFunc(Redisplay);
	Init();
	glutIdleFunc(Anim ? Idle : NULL);
	glutMainLoop();
	return 0;
}
