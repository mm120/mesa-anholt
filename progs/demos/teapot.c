/*
 * This program is under the GNU GPL.
 * Use at your own risk.
 *
 * written by David Bucciarelli (tech.hmw@plus.it)
 *            Humanware s.r.l.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/glut.h>
#include "../util/readtex.c"
#include "shadow.c"

#ifdef XMESA
#include "GL/xmesa.h"
static int fullscreen=1;
#endif

static int WIDTH=640;
static int HEIGHT=480;

static GLint T0 = 0;
static GLint Frames = 0;

#define BASESIZE 10.0

#define BASERES 12
#define TEAPOTRES 3

#ifndef M_PI
#define M_PI 3.1415926535
#endif

extern void shadowmatrix(GLfloat [4][4], GLfloat [4], GLfloat [4]);
extern void findplane(GLfloat [4], GLfloat [3], GLfloat [3], GLfloat [3]);


static int win=0;

static float obs[3]={5.0,0.0,1.0};
static float dir[3];
static float v=0.0;
static float alpha=-90.0;
static float beta=90.0;

static GLfloat baseshadow[4][4];
static GLfloat lightpos[4]={2.3,0.0,3.0,1.0};
static GLfloat lightdir[3]={-2.3,0.0,-3.0};
static GLfloat lightalpha=0.0;

static int fog=1;
static int bfcull=1;
static int usetex=1;
static int help=1;
static int joyavailable=0;
static int joyactive=0;

static GLuint t1id,t2id;
static GLuint teapotdlist,basedlist,lightdlist;

static void calcposobs(void)
{
  dir[0]=sin(alpha*M_PI/180.0);
  dir[1]=cos(alpha*M_PI/180.0)*sin(beta*M_PI/180.0);
  dir[2]=cos(beta*M_PI/180.0);

  obs[0]+=v*dir[0];
  obs[1]+=v*dir[1];
  obs[2]+=v*dir[2];
}

static void special(int k, int x, int y)
{
  switch(k) {
  case GLUT_KEY_LEFT:
    alpha-=2.0;
    break;
  case GLUT_KEY_RIGHT:
    alpha+=2.0;
    break;
  case GLUT_KEY_DOWN:
    beta-=2.0;
    break;
  case GLUT_KEY_UP:
    beta+=2.0;
    break;
  }
}

static void key(unsigned char k, int x, int y)
{
  switch(k) {
  case 27:
    exit(0);
    break;
    
  case 'a':
    v+=0.005;
    break;
  case 'z':
    v-=0.005;
    break;

  case 'j':
    joyactive=(!joyactive);
    break;
  case 'h':
    help=(!help);
    break;
  case 'f':
    fog=(!fog);
    break;
  case 't':
    usetex=(!usetex);
    break;
  case 'b':
    if(bfcull) {
      glDisable(GL_CULL_FACE);
      bfcull=0;
    } else {
      glEnable(GL_CULL_FACE);
      bfcull=1;
    }
    break;
#ifdef XMESA
  case ' ':
    XMesaSetFXmode(fullscreen ? XMESA_FX_FULLSCREEN : XMESA_FX_WINDOW);
    fullscreen=(!fullscreen);
    break;
#endif
  }
}

static void reshape(int w, int h) 
{
  WIDTH=w;
  HEIGHT=h;
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45.0,w/(float)h,0.2,40.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(0,0,w,h);
}

static void printstring(void *font, char *string)
{
  int len,i;

  len=(int)strlen(string);
  for(i=0;i<len;i++)
    glutBitmapCharacter(font,string[i]);
}

static void printhelp(void)
{
  glEnable(GL_BLEND);
  glColor4f(0.5,0.5,0.5,0.5);
  glRecti(40,40,600,440);
  glDisable(GL_BLEND);

  glColor3f(1.0,0.0,0.0);
  glRasterPos2i(300,420);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"Help");

  glRasterPos2i(60,390);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"h - Togle Help");
  glRasterPos2i(60,360);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"t - Togle Textures");
  glRasterPos2i(60,330);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"f - Togle Fog");
  glRasterPos2i(60,300);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"b - Togle Back face culling");
  glRasterPos2i(60,270);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"Arrow Keys - Rotate");
  glRasterPos2i(60,240);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"a - Increase velocity");
  glRasterPos2i(60,210);
  printstring(GLUT_BITMAP_TIMES_ROMAN_24,"z - Decrease velocity");

  glRasterPos2i(60,180);
  if(joyavailable)
    printstring(GLUT_BITMAP_TIMES_ROMAN_24,"j - Togle jostick control (Joystick control available)");
  else
    printstring(GLUT_BITMAP_TIMES_ROMAN_24,"(No Joystick control available)");
}

static void drawbase(void)
{
  int i,j;
  float x,y,dx,dy;

  glBindTexture(GL_TEXTURE_2D,t1id);

  dx=BASESIZE/BASERES;
  dy=-BASESIZE/BASERES;
  for(y=BASESIZE/2.0,j=0;j<BASERES;y+=dy,j++) {
    glBegin(GL_QUAD_STRIP);
    glColor3f(1.0,1.0,1.0);
    glNormal3f(0.0,0.0,1.0);
    for(x=-BASESIZE/2.0,i=0;i<BASERES;x+=dx,i++) {
      glTexCoord2f(x,y);
      glVertex3f(x,y,0.0);

      glTexCoord2f(x,y+dy);
      glVertex3f(x,y+dy,0.0);
    }
    glEnd();
  }
}

static void drawteapot(void)
{
  static float xrot=0.0;
  static float zrot=0.0;

  glPushMatrix();
  glRotatef(lightalpha,0.0,0.0,1.0);
  glMultMatrixf((GLfloat *)baseshadow);
  glRotatef(-lightalpha,0.0,0.0,1.0);

  glTranslatef(0.0,0.0,1.0);
  glRotatef(xrot,1.0,0.0,0.0);
  glRotatef(zrot,0.0,0.0,1.0);

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);

  glColor3f(0.0,0.0,0.0);
  glCallList(teapotdlist);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  if(usetex)
    glEnable(GL_TEXTURE_2D);

  glPopMatrix();

  glPushMatrix();
  glTranslatef(0.0,0.0,1.0);
  glRotatef(xrot,1.0,0.0,0.0);
  glRotatef(zrot,0.0,0.0,1.0);

  glCallList(teapotdlist);
  glPopMatrix();

  xrot+=2.0;
  zrot+=1.0;
}

static void drawlight1(void)
{
  glPushMatrix();
  glRotatef(lightalpha,0.0,0.0,1.0);
  glLightfv(GL_LIGHT0,GL_POSITION,lightpos);
  glLightfv(GL_LIGHT0,GL_SPOT_DIRECTION,lightdir);

  glPopMatrix();
}

static void drawlight2(void)
{
  glPushMatrix();
  glRotatef(lightalpha,0.0,0.0,1.0);
  glTranslatef(lightpos[0],lightpos[1],lightpos[2]);

  glDisable(GL_TEXTURE_2D);
  glCallList(lightdlist);
  if(usetex)
    glEnable(GL_TEXTURE_2D);
  
  glPopMatrix();

  lightalpha+=1.0;
}

static void dojoy(void)
{
#ifdef WIN32
  static UINT max[2]={0,0};
  static UINT min[2]={0xffffffff,0xffffffff},center[2];
  MMRESULT res;
  JOYINFO joy;

  res=joyGetPos(JOYSTICKID1,&joy);

  if(res==JOYERR_NOERROR) {
    joyavailable=1;

    if(max[0]<joy.wXpos)
      max[0]=joy.wXpos;
    if(min[0]>joy.wXpos)
      min[0]=joy.wXpos;
    center[0]=(max[0]+min[0])/2;

    if(max[1]<joy.wYpos)
      max[1]=joy.wYpos;
    if(min[1]>joy.wYpos)
      min[1]=joy.wYpos;
    center[1]=(max[1]+min[1])/2;

    if(joyactive) {
      if(fabs(center[0]-(float)joy.wXpos)>0.1*(max[0]-min[0]))
	alpha-=2.5*(center[0]-(float)joy.wXpos)/(max[0]-min[0]);
      if(fabs(center[1]-(float)joy.wYpos)>0.1*(max[1]-min[1]))
	beta+=2.5*(center[1]-(float)joy.wYpos)/(max[1]-min[1]);

      if(joy.wButtons & JOY_BUTTON1)
	v+=0.005;
      if(joy.wButtons & JOY_BUTTON2)
	v-=0.005;
    }
  } else
    joyavailable=0;
#endif
}

static void draw(void)
{
  static char frbuf[80] = "";

  dojoy();

  glEnable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

  if(usetex)
    glEnable(GL_TEXTURE_2D);
  else
    glDisable(GL_TEXTURE_2D);

  if(fog)
    glEnable(GL_FOG);
  else
    glDisable(GL_FOG);

  glEnable(GL_LIGHTING);

  glShadeModel(GL_SMOOTH);

  glPushMatrix();
  calcposobs();

  gluLookAt(obs[0],obs[1],obs[2],
	    obs[0]+dir[0],obs[1]+dir[1],obs[2]+dir[2],
	    0.0,0.0,1.0);

  drawlight1();
  glCallList(basedlist);
  drawteapot();
  drawlight2();
  glPopMatrix();
  
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_FOG);
  glShadeModel(GL_FLAT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-0.5,639.5,-0.5,479.5,-1.0,1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glColor3f(1.0,0.0,0.0);
  glRasterPos2i(10,10);
  printstring(GLUT_BITMAP_HELVETICA_18,frbuf);
  glRasterPos2i(350,470);
  printstring(GLUT_BITMAP_HELVETICA_10,"Teapot V1.2 Written by David Bucciarelli (tech.hmw@plus.it)");

  if(help)
    printhelp();

  reshape(WIDTH,HEIGHT);

  glutSwapBuffers();

   Frames++;

   {
      GLint t = glutGet(GLUT_ELAPSED_TIME);
      if (t - T0 >= 2000) {
         GLfloat seconds = (t - T0) / 1000.0;
         GLfloat fps = Frames / seconds;
         sprintf(frbuf, "Frame rate: %f", fps);
         T0 = t;
         Frames = 0;
      }
   }
}

static void inittextures(void)
{
  glGenTextures(1,&t1id);
  glBindTexture(GL_TEXTURE_2D,t1id);

  glPixelStorei(GL_UNPACK_ALIGNMENT,4);
  if (!LoadRGBMipmaps("../images/tile.rgb", GL_RGB)) {
    fprintf(stderr,"Error reading a texture.\n");
    exit(-1);
  }

  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

  glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

  glGenTextures(1,&t2id);
  glBindTexture(GL_TEXTURE_2D,t2id);

  if (!LoadRGBMipmaps("../images/bw.rgb", GL_RGB)) {
    fprintf(stderr,"Error reading a texture.\n");
    exit(-1);
  }

  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

  glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}

static void initlight(void)
{
  float lamb[4]={0.2,0.2,0.2,1.0};
  float lspec[4]={1.0,1.0,1.0,1.0};

  glLightf(GL_LIGHT0,GL_SPOT_CUTOFF,70.0);
  glLightf(GL_LIGHT0,GL_SPOT_EXPONENT,20.0);
  glLightfv(GL_LIGHT0,GL_AMBIENT,lamb);
  glLightfv(GL_LIGHT0,GL_SPECULAR,lspec);

  glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,20.0);
  glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,lspec);

  glEnable(GL_LIGHT0);
}

static void initdlists(void)
{
  GLUquadricObj *lcone,*lbase;
  GLfloat plane[4];
  GLfloat v0[3]={0.0,0.0,0.0};
  GLfloat v1[3]={1.0,0.0,0.0};
  GLfloat v2[3]={0.0,1.0,0.0};

  findplane(plane,v0,v1,v2);
  shadowmatrix(baseshadow,plane,lightpos);

  teapotdlist=glGenLists(1);
  glNewList(teapotdlist,GL_COMPILE);
  glRotatef(90.0,1.0,0.0,0.0);
  glCullFace(GL_FRONT);
  glBindTexture(GL_TEXTURE_2D,t2id);
  glutSolidTeapot(0.75);
  glCullFace(GL_BACK);
  glEndList();

  basedlist=glGenLists(1);
  glNewList(basedlist,GL_COMPILE);
  drawbase();
  glEndList();

  lightdlist=glGenLists(1);
  glNewList(lightdlist,GL_COMPILE);
  glDisable(GL_LIGHTING);
 
  lcone=gluNewQuadric();
  lbase=gluNewQuadric();
  glRotatef(45.0,0.0,1.0,0.0);

  glColor3f(1.0,1.0,1.0);
  glCullFace(GL_FRONT);
  gluDisk(lbase,0.0,0.2,12.0,1.0);
  glCullFace(GL_BACK);

  glColor3f(0.5,0.0,0.0);
  gluCylinder(lcone,0.2,0.0,0.5,12,1);

  gluDeleteQuadric(lcone);
  gluDeleteQuadric(lbase);

  glEnable(GL_LIGHTING);
  glEndList();
}

int main(int ac, char **av)
{
  float fogcolor[4]={0.025,0.025,0.025,1.0};

  fprintf(stderr,"Teapot V1.2\nWritten by David Bucciarelli (tech.hmw@plus.it)\n");

  /*
    if(!SetPriorityClass(GetCurrentProcess(),REALTIME_PRIORITY_CLASS)) {
    fprintf(stderr,"Error setting the process class.\n");
    return 0;
    }

    if(!SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL)) {
    fprintf(stderr,"Error setting the process priority.\n");
    return 0;
    }
    */

  glutInitWindowPosition(0,0);
  glutInitWindowSize(WIDTH,HEIGHT);
  glutInit(&ac,av);

  glutInitDisplayMode(GLUT_RGB|GLUT_DEPTH|GLUT_DOUBLE);

  if(!(win=glutCreateWindow("Teapot"))) {
    fprintf(stderr,"Error, couldn't open window\n");
	return -1;
  }

  reshape(WIDTH,HEIGHT);

  glShadeModel(GL_SMOOTH);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);

  glEnable(GL_FOG);
  glFogi(GL_FOG_MODE,GL_EXP2);
  glFogfv(GL_FOG_COLOR,fogcolor);

  glFogf(GL_FOG_DENSITY,0.04);
  glHint(GL_FOG_HINT,GL_NICEST);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

  calcposobs();

  inittextures();
  initlight();

  initdlists();

  glClearColor(fogcolor[0],fogcolor[1],fogcolor[2],fogcolor[3]);

  glutReshapeFunc(reshape);
  glutDisplayFunc(draw);
  glutKeyboardFunc(key);
  glutSpecialFunc(special);
  glutIdleFunc(draw);

  glutMainLoop();

  return 0;
}
