/* $Id: multiarb.c,v 1.8 2000/12/24 22:53:54 pesco Exp $ */

/*
 * GL_ARB_multitexture demo
 *
 * Command line options:
 *    -info      print GL implementation information
 *
 *
 * Brian Paul  November 1998  This program is in the public domain.
 */

/*
 * $Log: multiarb.c,v $
 * Revision 1.8  2000/12/24 22:53:54  pesco
 * * demos/Makefile.am (INCLUDES): Added -I$(top_srcdir)/util.
 * * demos/Makefile.X11, demos/Makefile.BeOS-R4, demos/Makefile.cygnus:
 * Essentially the same.
 * Program files updated to include "readtex.c", not "../util/readtex.c".
 * * demos/reflect.c: Likewise for "showbuffer.c".
 *
 *
 * * Makefile.am (EXTRA_DIST): Added top-level regular files.
 *
 * * include/GL/Makefile.am (INC_X11): Added glxext.h.
 *
 *
 * * src/GGI/include/ggi/mesa/Makefile.am (EXTRA_HEADERS): Include
 * Mesa GGI headers in dist even if HAVE_GGI is not given.
 *
 * * configure.in: Look for GLUT and demo source dirs in $srcdir.
 *
 * * src/swrast/Makefile.am (libMesaSwrast_la_SOURCES): Set to *.[ch].
 * More source list updates in various Makefile.am's.
 *
 * * Makefile.am (dist-hook): Remove CVS directory from distribution.
 * (DIST_SUBDIRS): List all possible subdirs here.
 * (SUBDIRS): Only list subdirs selected for build again.
 * The above two applied to all subdir Makefile.am's also.
 *
 * Revision 1.7  2000/11/01 16:02:01  brianp
 * print number of texture units
 *
 * Revision 1.6  2000/05/23 23:21:00  brianp
 * set default window pos
 *
 * Revision 1.5  2000/02/02 17:31:45  brianp
 * changed > to >=
 *
 * Revision 1.4  2000/02/02 01:07:21  brianp
 * limit Drift to [0, 1]
 *
 * Revision 1.3  1999/10/21 16:40:32  brianp
 * added -info command line option
 *
 * Revision 1.2  1999/10/13 12:02:13  brianp
 * use texture objects now
 *
 * Revision 1.1.1.1  1999/08/19 00:55:40  jtg
 * Imported sources
 *
 * Revision 1.3  1999/03/28 18:20:49  brianp
 * minor clean-up
 *
 * Revision 1.2  1998/11/05 04:34:04  brianp
 * moved image files to ../images/ directory
 *
 * Revision 1.1  1998/11/03 01:36:33  brianp
 * Initial revision
 *
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glut.h>

#include "readtex.c"   /* I know, this is a hack. */

#define TEXTURE_1_FILE "../images/girl.rgb"
#define TEXTURE_2_FILE "../images/reflect.rgb"

#define TEX0 1
#define TEX1 2
#define TEXBOTH 3
#define ANIMATE 10
#define QUIT 100

static GLboolean Animate = GL_TRUE;

static GLfloat Drift = 0.0;
static GLfloat Xrot = 20.0, Yrot = 30.0, Zrot = 0.0;



static void Idle( void )
{
   if (Animate) {
      Drift += 0.05;
      if (Drift >= 1.0)
         Drift = 0.0;

#ifdef GL_ARB_multitexture
      glActiveTextureARB(GL_TEXTURE0_ARB);
#endif
      glMatrixMode(GL_TEXTURE);
      glLoadIdentity();
      glTranslatef(Drift, 0.0, 0.0);
      glMatrixMode(GL_MODELVIEW);

#ifdef GL_ARB_multitexture
      glActiveTextureARB(GL_TEXTURE1_ARB);
#endif
      glMatrixMode(GL_TEXTURE);
      glLoadIdentity();
      glTranslatef(0.0, Drift, 0.0);
      glMatrixMode(GL_MODELVIEW);

      glutPostRedisplay();
   }
}


static void DrawObject(void)
{
   glBegin(GL_QUADS);

#ifdef GL_ARB_multitexture
   glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0, 0.0);
   glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0, 0.0);
   glVertex2f(-1.0, -1.0);

   glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 2.0, 0.0);
   glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0, 0.0);
   glVertex2f(1.0, -1.0);

   glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 2.0, 2.0);
   glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0, 1.0);
   glVertex2f(1.0, 1.0);

   glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0, 2.0);
   glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0, 1.0);
   glVertex2f(-1.0, 1.0);
#else
   glTexCoord2f(0.0, 0.0);
   glVertex2f(-1.0, -1.0);

   glTexCoord2f(1.0, 0.0);
   glVertex2f(1.0, -1.0);

   glTexCoord2f(1.0, 1.0);
   glVertex2f(1.0, 1.0);

   glTexCoord2f(0.0, 1.0);
   glVertex2f(-1.0, 1.0);
#endif

   glEnd();
}



static void Display( void )
{
   glClear( GL_COLOR_BUFFER_BIT );

   glPushMatrix();
      glRotatef(Xrot, 1.0, 0.0, 0.0);
      glRotatef(Yrot, 0.0, 1.0, 0.0);
      glRotatef(Zrot, 0.0, 0.0, 1.0);
      glScalef(5.0, 5.0, 5.0);
      DrawObject();
   glPopMatrix();

   glutSwapBuffers();
}


static void Reshape( int width, int height )
{
   glViewport( 0, 0, width, height );
   glMatrixMode( GL_PROJECTION );
   glLoadIdentity();
   glFrustum( -1.0, 1.0, -1.0, 1.0, 10.0, 100.0 );
   /*glOrtho( -6.0, 6.0, -6.0, 6.0, 10.0, 100.0 );*/
   glMatrixMode( GL_MODELVIEW );
   glLoadIdentity();
   glTranslatef( 0.0, 0.0, -70.0 );
}


static void ModeMenu(int entry)
{
   GLboolean enable0 = GL_FALSE, enable1 = GL_FALSE;
   if (entry==TEX0) {
      enable0 = GL_TRUE;
   }
   else if (entry==TEX1) {
      enable1 = GL_TRUE;
   }
   else if (entry==TEXBOTH) {
      enable0 = GL_TRUE;
      enable1 = GL_TRUE;
   }
   else if (entry==ANIMATE) {
      Animate = !Animate;
   }
   else if (entry==QUIT) {
      exit(0);
   }

   if (entry != ANIMATE) {
#ifdef GL_ARB_multitexture
      glActiveTextureARB(GL_TEXTURE0_ARB);
#endif
      if (enable0) {
         glEnable(GL_TEXTURE_2D);
      }
      else
         glDisable(GL_TEXTURE_2D);

#ifdef GL_ARB_multitexture
      glActiveTextureARB(GL_TEXTURE1_ARB);
#endif
      if (enable1) {
         glEnable(GL_TEXTURE_2D);
      }
      else
         glDisable(GL_TEXTURE_2D);
   }

   glutPostRedisplay();
}


static void Key( unsigned char key, int x, int y )
{
   (void) x;
   (void) y;
   switch (key) {
      case 27:
         exit(0);
         break;
   }
   glutPostRedisplay();
}


static void SpecialKey( int key, int x, int y )
{
   float step = 3.0;
   (void) x;
   (void) y;

   switch (key) {
      case GLUT_KEY_UP:
         Xrot += step;
         break;
      case GLUT_KEY_DOWN:
         Xrot -= step;
         break;
      case GLUT_KEY_LEFT:
         Yrot += step;
         break;
      case GLUT_KEY_RIGHT:
         Yrot -= step;
         break;
   }
   glutPostRedisplay();
}


static void Init( int argc, char *argv[] )
{
   GLuint texObj[2];
   GLint units;

   const char *exten = (const char *) glGetString(GL_EXTENSIONS);
   if (!strstr(exten, "GL_ARB_multitexture")) {
      printf("Sorry, GL_ARB_multitexture not supported by this renderer.\n");
      exit(1);
   }

   glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);
   printf("%d texture units supported\n", units);

   /* allocate two texture objects */
   glGenTextures(2, texObj);

   /* setup texture obj 0 */
   glBindTexture(GL_TEXTURE_2D, texObj[0]);
#ifdef LINEAR_FILTER
   /* linear filtering looks much nicer but is much slower for Mesa */
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#else
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif

   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

   if (!LoadRGBMipmaps(TEXTURE_1_FILE, GL_RGB)) {
      printf("Error: couldn't load texture image\n");
      exit(1);
   }


   /* setup texture obj 1 */
   glBindTexture(GL_TEXTURE_2D, texObj[1]);
#ifdef LINEAR_FILTER
   /* linear filtering looks much nicer but is much slower for Mesa */
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#else
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif

   glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

   if (!LoadRGBMipmaps(TEXTURE_2_FILE, GL_RGB)) {
      printf("Error: couldn't load texture image\n");
      exit(1);
   }


   /* now bind the texture objects to the respective texture units */
#ifdef GL_ARB_multitexture
   glActiveTextureARB(GL_TEXTURE0_ARB);
   glBindTexture(GL_TEXTURE_2D, texObj[0]);
   glActiveTextureARB(GL_TEXTURE1_ARB);
   glBindTexture(GL_TEXTURE_2D, texObj[1]);
#endif

   glShadeModel(GL_FLAT);
   glClearColor(0.3, 0.3, 0.4, 1.0);

   ModeMenu(TEXBOTH);

   if (argc > 1 && strcmp(argv[1], "-info")==0) {
      printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
      printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
      printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
      printf("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
   }
}


int main( int argc, char *argv[] )
{
   glutInit( &argc, argv );
   glutInitWindowSize( 300, 300 );
   glutInitWindowPosition( 0, 0 );
   glutInitDisplayMode( GLUT_RGB | GLUT_DOUBLE );
   glutCreateWindow(argv[0] );

   Init( argc, argv );

   glutReshapeFunc( Reshape );
   glutKeyboardFunc( Key );
   glutSpecialFunc( SpecialKey );
   glutDisplayFunc( Display );
   glutIdleFunc( Idle );

   glutCreateMenu(ModeMenu);
   glutAddMenuEntry("Texture 0", TEX0);
   glutAddMenuEntry("Texture 1", TEX1);
   glutAddMenuEntry("Multi-texture", TEXBOTH);
   glutAddMenuEntry("Toggle Animation", ANIMATE);
   glutAddMenuEntry("Quit", QUIT);
   glutAttachMenu(GLUT_RIGHT_BUTTON);

   glutMainLoop();
   return 0;
}
