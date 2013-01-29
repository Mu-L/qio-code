/*
============================================================================
Copyright (C) 2012 V.

This file is part of Qio source code.

Qio source code is free software; you can redistribute it 
and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

Qio source code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA,
or simply visit <http://www.gnu.org/licenses/>.
============================================================================
*/
// gl_main.cpp - SDL / openGL backend

#include "gl_local.h"
#include "gl_shader.h"
#include <api/coreAPI.h>
#include <api/rbAPI.h>
#include <api/iFaceMgrAPI.h>
#include <api/textureAPI.h>
#include <api/mtrAPI.h>
#include <api/mtrStageAPI.h>
#include <api/sdlSharedAPI.h>
#include <shared/r2dVert.h>
#include <math/matrix.h>
#include <math/axis.h>
#include <math/aabb.h>
#include <math/plane.h>

#include <materialSystem/mat_public.h> // alphaFunc_e etc
#include <renderer/rVertexBuffer.h>
#include <renderer/rIndexBuffer.h>
#include <renderer/rPointBuffer.h>

#include <shared/cullType.h>

#include <shared/autoCvar.h>
#include <api/rLightAPI.h>
#include <api/occlusionQueryAPI.h>

#include <shared/byteRGB.h>

static aCvar_c rb_showTris("rb_showTris","0");
// use a special GLSL shader to show normal vectors as colors
static aCvar_c rb_showNormalColors("rb_showNormalColors","0");
static aCvar_c gl_callGLFinish("gl_callGLFinish","0");
static aCvar_c gl_checkForGLErrors("gl_checkForGLErrors","1");
static aCvar_c rb_printMemcpyVertexArrayBottleneck("rb_printMemcpyVertexArrayBottleneck","0");
static aCvar_c rb_gpuTexGens("rb_gpuTexGens","0");
// always use GLSL shaders, even if they are not needed for any material effects
static aCvar_c gl_alwaysUseGLSLShaders("gl_alwaysUseGLSLShaders","0");

#define MAX_TEXTURE_SLOTS 32

#ifndef offsetof
#ifdef  _WIN64
#define offsetof(s,m)   (size_t)( (ptrdiff_t)&reinterpret_cast<const volatile char&>((((s *)0)->m)) )
#else
#define offsetof(s,m)   (size_t)&reinterpret_cast<const volatile char&>((((s *)0)->m))
#endif
#endif // not defined offsetof

class glOcclusionQuery_c : public occlusionQueryAPI_i {
	u32 oqHandle;
	bool waitingForResult;
	mutable u32 lastResult;
public:
	glOcclusionQuery_c();
	~glOcclusionQuery_c();
	virtual void generateOcclusionQuery();
	virtual void assignSphereQuery(const vec3_c &p, float radius);
	virtual u32 getNumSamplesPassed() const;
	virtual bool isResultAvailable() const;
	virtual u32 getPreviousResult() const;
	virtual u32 waitForLatestResult() const;
};

struct texState_s {
	bool enabledTexture2D;
	bool texCoordArrayEnabled;
	u32 index;
	matrixExt_c mat;

	texState_s() {
		enabledTexture2D = false;
		texCoordArrayEnabled = false;
		index = 0;
	}
};
#define CHECK_GL_ERRORS checkForGLErrorsInternal(__FUNCTION__,__LINE__);

class rbSDLOpenGL_c : public rbAPI_i {
	// gl state
	texState_s texStates[MAX_TEXTURE_SLOTS];
	int curTexSlot;
	int highestTCSlotUsed;
	// materials
	//safePtr_c<mtrAPI_i> lastMat;
	mtrAPI_i *lastMat;
	textureAPI_i *lastLightmap;
	bool bindVertexColors;
	bool bHasVertexColors;
	// matrices
	matrix_c worldModelMatrix;
	matrix_c resultMatrix;
	axis_c viewAxis; // viewer's camera axis
	vec3_c camOriginWorldSpace;
	vec3_c camOriginEntitySpace;
	bool usingWorldSpace;
	axis_c entityAxis;
	vec3_c entityOrigin;
	matrix_c entityMatrix;
	matrix_c entityMatrixInverse;
	
	bool boundVBOVertexColors;
	const rVertexBuffer_c *boundVBO;

	const class rIndexBuffer_c *boundIBO;
	u32 boundGPUIBO;

	bool backendInitialized;

	float timeNowSeconds;
	bool isMirror;

	// counters
	u32 c_frame_vbsReusedByDifferentDrawCall;

public:
	rbSDLOpenGL_c() {
		lastMat = 0;
		lastLightmap = 0;
		bindVertexColors = 0;
		curTexSlot = 0;
		highestTCSlotUsed = 0;
		boundVBO = 0;
		boundGPUIBO = 0;
		boundIBO = 0;
		backendInitialized = false;
		curLight = 0;
		isMirror = false;
	}
	virtual backEndType_e getType() const {
		return BET_GL;
	}
	void checkForGLErrorsInternal(const char *functionName, u32 line) {
		if(gl_checkForGLErrors.getInt() == 0)
			return;

		int		err;
		char	s[64];

		err = glGetError();
		if ( err == GL_NO_ERROR ) {
			return;
		}
		switch( err ) {
			case GL_INVALID_ENUM:
				strcpy( s, "GL_INVALID_ENUM" );
				break;
			case GL_INVALID_VALUE:
				strcpy( s, "GL_INVALID_VALUE" );
				break;
			case GL_INVALID_OPERATION:
				strcpy( s, "GL_INVALID_OPERATION" );
				break;
			case GL_STACK_OVERFLOW:
				strcpy( s, "GL_STACK_OVERFLOW" );
				break;
			case GL_STACK_UNDERFLOW:
				strcpy( s, "GL_STACK_UNDERFLOW" );
				break;
			case GL_OUT_OF_MEMORY:
				strcpy( s, "GL_OUT_OF_MEMORY" );
				break;
			default:
				Com_sprintf( s, sizeof(s), "%i", err);
				break;
		}
		g_core->Print("GL_CheckErrors (%s:%i): %s\n", functionName, line, s );
	}
	virtual bool isGLSLSupportAvaible() const {
		// TODO: do a better check to see if GLSL shaders are supported
		if(glCreateProgram == 0) {
			return false;
		}
		if(glCreateShader == 0) {
			return false;
		}
	}
	// returns true if "texgen environment" q3 shader effect can be done on GPU
	virtual bool gpuTexGensSupported() const {
		if(rb_gpuTexGens.getInt() == 0)
			return false;
		if(isGLSLSupportAvaible() == false)
			return false;
		return true;
	}
	// GL_COLOR_ARRAY changes
	bool colorArrayActive;
	void enableColorArray() {
		if(colorArrayActive == true)
			return;
		glEnableClientState(GL_COLOR_ARRAY);
		colorArrayActive = true;
	}
	void disableColorArray() {
		if(colorArrayActive == false)
			return;
		glDisableClientState(GL_COLOR_ARRAY);
		colorArrayActive = false;
	}
	//
	// texture changes
	//
	void selectTex(int slot) {
		if(slot+1 > highestTCSlotUsed) {
			highestTCSlotUsed = slot+1;
		}
		if(curTexSlot != slot) {
			glActiveTexture(GL_TEXTURE0 + slot);
			glClientActiveTexture(GL_TEXTURE0 + slot);
			curTexSlot = slot;
		}
	}
	void bindTex(int slot, u32 tex) {
		texState_s *s = &texStates[slot];
		if(s->enabledTexture2D == true && s->index == tex)
			return;

		selectTex(slot);
		if(s->enabledTexture2D == false) {
			glEnable(GL_TEXTURE_2D);
			s->enabledTexture2D = true;
		}
		if(s->index != tex) {
			glBindTexture(GL_TEXTURE_2D,tex);
			s->index = tex;
		}
	}
	void unbindTex(int slot) {
		texState_s *s = &texStates[slot];
		if(s->enabledTexture2D == false && s->index == 0)
			return;

		selectTex(slot);
		if(s->enabledTexture2D == true) {
			glDisable(GL_TEXTURE_2D);
			s->enabledTexture2D = false;
		}
		if(s->index != 0) {
			glBindTexture(GL_TEXTURE_2D,0);
			s->index = 0;
		}
	}
	void disableAllTextures() {
		texState_s *s = &texStates[0];
		for(int i = 0; i < highestTCSlotUsed; i++, s++) {
			if(s->enabledTexture2D == false && s->index == 0)
				continue;
			selectTex(i);
			if(s->index != 0) {
				glBindTexture(GL_TEXTURE_2D,0);
				s->index = 0;
			}
			if(s->enabledTexture2D) {
				glDisable(GL_TEXTURE_2D);
				s->enabledTexture2D = false;
			}
		}
		highestTCSlotUsed = -1;
	}
	//
	// depthRange changes
	//
	float depthRangeNearVal;
	float depthRangeFarVal;
	void setDepthRange(float nearVal, float farVal) {
		if(nearVal == depthRangeNearVal && farVal == depthRangeFarVal)
			return; // no change
		glDepthRange(nearVal,farVal);
		depthRangeNearVal = nearVal;
		depthRangeFarVal = farVal;
	}
	//
	// alphaFunc changes
	//
	alphaFunc_e prevAlphaFunc;
	void setAlphaFunc(alphaFunc_e newAlphaFunc) {
		if(prevAlphaFunc == newAlphaFunc) {
			return; // no change
		}
		if(newAlphaFunc == AF_NONE) {
			glDisable(GL_ALPHA_TEST);
		} else if(newAlphaFunc == AF_GT0) {
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc( GL_GREATER, 0.0f ); 
		} else if(newAlphaFunc == AF_GE128) {
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc( GL_GREATER, 0.5f ); 
		} else if(newAlphaFunc == AF_LT128) {
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc( GL_LESS, 0.5f ); 
		}
		prevAlphaFunc = newAlphaFunc;
	}
	void turnOffAlphaFunc() {
		setAlphaFunc(AF_NONE);
	}	
	//
	// GL_BLEND changes
	//
	bool blendEnable;
	void enableGLBlend() {
		if(blendEnable)
			return;
		glEnable(GL_BLEND);
		blendEnable = true;
	}
	void disableGLBlend() {
		if(blendEnable==false)
			return;
		glDisable(GL_BLEND);
		blendEnable = false;

		blendSrc = BM_NOT_SET;
		blendDst = BM_NOT_SET;
	}
	//
	// GL_NORMAL_ARRAY changes
	//
	bool glNormalArrayEnabled;
	void enableNormalArray() {
		if(glNormalArrayEnabled)
			return;
		glEnable(GL_NORMAL_ARRAY);
		glNormalArrayEnabled = true;
	}
	void disableNormalArray() {
		if(glNormalArrayEnabled==false)
			return;
		glDisable(GL_NORMAL_ARRAY);
		glNormalArrayEnabled = false;
	}
	static int blendModeEnumToGLBlend(int in) {
		static int blendTable [] = {
			0, // BM_NOT_SET
			GL_ZERO,
			GL_ONE,
			GL_ONE_MINUS_SRC_COLOR,
			GL_ONE_MINUS_DST_COLOR,
			GL_ONE_MINUS_SRC_ALPHA,
			GL_ONE_MINUS_DST_ALPHA,
			GL_DST_COLOR,
			GL_DST_ALPHA,
			GL_SRC_COLOR,
			GL_SRC_ALPHA,
			GL_SRC_ALPHA_SATURATE,
		};
		return blendTable[in];
	}
	short blendSrc;
	short blendDst;
	void setBlendFunc( short src, short dst ) {
		if( blendSrc != src || blendDst != dst ) {
			blendSrc = src;
			blendDst = dst;
			if(src == BM_NOT_SET && dst == BM_NOT_SET) {
				//setGLDepthMask(true);
				disableGLBlend(); //glDisable( GL_BLEND );
			} else {
				enableGLBlend(); // glEnable( GL_BLEND );
				glBlendFunc( blendModeEnumToGLBlend(blendSrc), blendModeEnumToGLBlend(blendDst) );
				//setGLDepthMask(false);
			}
		}
	}
	// tex coords arrays
	void enableTexCoordArrayForCurrentTexSlot() {
		texState_s *s = &texStates[curTexSlot];
		if(s->texCoordArrayEnabled==true)
			return;
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		s->texCoordArrayEnabled = true;
	}
	void disableTexCoordArrayForCurrentTexSlot() {
		texState_s *s = &texStates[curTexSlot];
		if(s->texCoordArrayEnabled==false)
			return;
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		s->texCoordArrayEnabled = false;
	}
	void disableTexCoordArrayForTexSlot(u32 slotNum) {
		texState_s *s = &texStates[slotNum];
		if(s->texCoordArrayEnabled==false)
			return;
		selectTex(slotNum);
		disableTexCoordArrayForCurrentTexSlot();
	}
	// texture matrices
	void turnOffTextureMatrices() {
		for(u32 i = 0; i < MAX_TEXTURE_SLOTS; i++) {
			setTextureMatrixIdentity(i);
		}
	}
	void setTextureMatrixCustom(u32 slot, const matrix_c &mat) {
		texState_s *ts = &this->texStates[slot];
		if(ts->mat.set(mat) == false)
			return; // no change

		this->selectTex(slot);
		glPushAttrib(GL_TRANSFORM_BIT);
		glMatrixMode(GL_TEXTURE);
		glLoadMatrixf(mat);
		glPopAttrib();
	}
	void setTextureMatrixIdentity(u32 slot) {
		texState_s *ts = &this->texStates[slot];
		if(ts->mat.isIdentity())
			return; // no change
		ts->mat.setIdentity();

		this->selectTex(slot);
		glPushAttrib(GL_TRANSFORM_BIT);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glPopAttrib();
	}
	bool polygonOffsetEnabled;
	// polygon offset (for decals)
	void setPolygonOffset(float factor, float units) {
		if(polygonOffsetEnabled == false) {
			polygonOffsetEnabled = true;
			glEnable(GL_POLYGON_OFFSET_FILL);
		}
		glPolygonOffset(factor,units);
	}
	void turnOffPolygonOffset() {
		if(polygonOffsetEnabled == true) {
			glDisable(GL_POLYGON_OFFSET_FILL);
			polygonOffsetEnabled = false;
		}
	}
	// glDepthMask
	bool bDepthMask;
	void setGLDepthMask(bool bOn) {
		if(bOn == bDepthMask) {
			return;
		}
		bDepthMask = bOn;
		glDepthMask(bDepthMask);
	}
	// GL_CULL
	cullType_e prevCullType;
	void glCull(cullType_e cullType) {
		if(prevCullType == cullType) {
			return;
		}
		prevCullType = cullType;
		if(cullType == CT_TWO_SIDED) {
			glDisable(GL_CULL_FACE);
		} else {
			glEnable( GL_CULL_FACE );
			if(isMirror) {
				// swap CT_FRONT with CT_BACK for mirror views
				if(cullType == CT_BACK_SIDED) {
					glCullFace(GL_FRONT);
				} else {
					glCullFace(GL_BACK);
				}
			} else {
				if(cullType == CT_BACK_SIDED) {
					glCullFace(GL_BACK);
				} else {
					glCullFace(GL_FRONT);
				}
			}
		}
	}
	// GL_STENCIL_TEST
	bool stencilTestEnabled;
	void setGLStencilTest(bool bOn) {
		if(stencilTestEnabled == bOn)
			return;
		stencilTestEnabled = bOn;
		if(bOn) {
			glEnable(GL_STENCIL_TEST);
		} else {
			glDisable(GL_STENCIL_TEST);
		}
	}
	virtual void setMaterial(class mtrAPI_i *mat, class textureAPI_i *lightmap) {
		lastMat = mat;
		lastLightmap = lightmap;
	}
	virtual void unbindMaterial() {
		disableAllTextures();
		turnOffAlphaFunc();
		disableColorArray();	
		turnOffPolygonOffset();
		turnOffTextureMatrices();
		setBlendFunc(BM_NOT_SET,BM_NOT_SET);
		lastMat = 0;
		lastLightmap = 0;
	}
	virtual void setColor4(const float *rgba)  {
		if(rgba == 0) {
			float def[] = { 1, 1, 1, 1 };
			rgba = def;
		}
		glColor4fv(rgba);
	}
	virtual void setBindVertexColors(bool bBindVertexColors) {
		this->bHasVertexColors = bBindVertexColors;
		this->bindVertexColors = bBindVertexColors;
	}
	bool bBoundLightmapCoordsToFirstTextureSlot;
	void bindVertexBuffer(const class rVertexBuffer_c *verts, bool bindLightmapCoordsToFirstTextureSlot = false) {
		if(boundVBO == verts) {
			if(boundVBOVertexColors == bindVertexColors) {
				if(bBoundLightmapCoordsToFirstTextureSlot == bindLightmapCoordsToFirstTextureSlot) {
					c_frame_vbsReusedByDifferentDrawCall++;
					return; // already bound
				}
			} else {

			}
		}
		u32 h = verts->getInternalHandleU32();
		glBindBuffer(GL_ARRAY_BUFFER,h);
		if(h == 0) {
			// buffer wasnt uploaded to GPU
			glVertexPointer(3,GL_FLOAT,sizeof(rVert_c),verts->getArray());
			selectTex(0);
			enableTexCoordArrayForCurrentTexSlot();
			if(bindLightmapCoordsToFirstTextureSlot) {
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),&verts->getArray()->lc.x);
				disableTexCoordArrayForTexSlot(1);
			} else {
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),&verts->getArray()->tc.x);
				selectTex(1);
				enableTexCoordArrayForCurrentTexSlot();
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),&verts->getArray()->lc.x);
			}
			if(bindVertexColors) {
				enableColorArray();
				glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(rVert_c),&verts->getArray()->color[0]);
			} else {
				disableColorArray();
			}
			enableNormalArray();
			glNormalPointer(GL_FLOAT,sizeof(rVert_c),&verts->getArray()->normal.x);
		} else {
			glVertexPointer(3,GL_FLOAT,sizeof(rVert_c),0);
			selectTex(0);
			enableTexCoordArrayForCurrentTexSlot();
			if(bindLightmapCoordsToFirstTextureSlot) {
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),(void*)offsetof(rVert_c,lc));
				disableTexCoordArrayForTexSlot(1);
			} else {
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),(void*)offsetof(rVert_c,tc));
				selectTex(1);
				enableTexCoordArrayForCurrentTexSlot();
				glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),(void*)offsetof(rVert_c,lc));
			}
			if(bindVertexColors) {
				enableColorArray();
				glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(rVert_c),(void*)offsetof(rVert_c,color));
			} else {
				disableColorArray();
			}
			enableNormalArray();
			glNormalPointer(GL_FLOAT,sizeof(rVert_c),(void*)offsetof(rVert_c,normal));
		}
		boundVBOVertexColors = bindVertexColors;
		bBoundLightmapCoordsToFirstTextureSlot = bindLightmapCoordsToFirstTextureSlot;
		boundVBO = verts;
		CHECK_GL_ERRORS;
	}
	void unbindVertexBuffer() {
		if(boundVBO == 0)
			return;
		if(boundVBO->getInternalHandleU32()) {
			glBindBuffer(GL_ARRAY_BUFFER,0);
		}
		glVertexPointer(3,GL_FLOAT,sizeof(rVert_c),0);
		selectTex(0);
		disableTexCoordArrayForCurrentTexSlot();
		glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),0);
		selectTex(1);
		disableTexCoordArrayForCurrentTexSlot();
		glTexCoordPointer(2,GL_FLOAT,sizeof(rVert_c),0);
		glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(rVert_c),0);
		disableColorArray();
		boundVBO = 0;
		CHECK_GL_ERRORS;
	}
	void unbindIBO() {
		if(boundGPUIBO) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
			boundGPUIBO = 0;
		}		
		boundIBO = 0;
	}
	void bindIBO(const class rIndexBuffer_c *indices) {
		if(boundIBO == indices)
			return;
		if(indices == 0) {
			unbindIBO();
			return;
		}
		u32 h = indices->getInternalHandleU32();
		if(h != boundGPUIBO) {
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,h);
			boundGPUIBO = h;
		}
		boundIBO = indices;
	}
	void drawCurIBO() {
		if(boundGPUIBO == 0) {
			glDrawElements(GL_TRIANGLES, boundIBO->getNumIndices(), boundIBO->getGLIndexType(), boundIBO->getVoidPtr());
		} else {
			glDrawElements(GL_TRIANGLES, boundIBO->getNumIndices(), boundIBO->getGLIndexType(), 0);
		}
		CHECK_GL_ERRORS;
		if(gl_callGLFinish.getInt()==2) {
			glFinish();
		}
	}
	virtual void draw2D(const struct r2dVert_s *verts, u32 numVerts, const u16 *indices, u32 numIndices)  {
		stopDrawingShadowVolumes();
		bindShader(0);
		CHECK_GL_ERRORS;

		glVertexPointer(2,GL_FLOAT,sizeof(r2dVert_s),verts);
		selectTex(0);
		enableTexCoordArrayForCurrentTexSlot();
		CHECK_GL_ERRORS;
		glTexCoordPointer(2,GL_FLOAT,sizeof(r2dVert_s),&verts->texCoords.x);
		disableNormalArray();
		CHECK_GL_ERRORS;
		if(lastMat) {
			// NOTE: this is the way Q3 draws all the 2d menu graphics
			// (it worked before with bigchars.tga, even when the bigchars
			// shader was missing!!!)
			//glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
			setBlendFunc(BM_SRC_ALPHA,BM_ONE_MINUS_SRC_ALPHA);
			for(u32 i = 0; i < lastMat->getNumStages(); i++) {
				const mtrStageAPI_i *s = lastMat->getStage(i);
				setAlphaFunc(s->getAlphaFunc());
				//const blendDef_s &bd = s->getBlendDef();
				//setBlendFunc(bd.src,bd.dst);
				textureAPI_i *t = s->getTexture(this->timeNowSeconds);
				bindTex(0,t->getInternalHandleU32());
				CHECK_GL_ERRORS;
				glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, indices);
				CHECK_GL_ERRORS;
			}
			//glDisable(GL_BLEND);
		} else {
			glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, indices);
		}
	}
	const rLightAPI_i *curLight;
	bool bDrawOnlyOnDepthBuffer;
	bool bDrawingShadowVolumes;
	virtual void setCurLight(const class rLightAPI_i *light) {
		this->curLight = light;
	}
	virtual void setBDrawOnlyOnDepthBuffer(bool bNewDrawOnlyOnDepthBuffer) {
		bDrawOnlyOnDepthBuffer = bNewDrawOnlyOnDepthBuffer;
	}
	void bindShader(class glShader_c *newShader) {
		if(newShader == 0) {
			glUseProgram(0);
		} else {
			glUseProgram(newShader->getGLHandle());
			if(newShader->sColorMap != -1) {
				glUniform1i(newShader->sColorMap,0);
			}
			if(newShader->sLightMap != -1) {
				glUniform1i(newShader->sLightMap,1);
			}
			if(curLight) {
				if(newShader->uLightOrigin != -1) {
					const vec3_c &xyz = curLight->getOrigin();
					if(usingWorldSpace) {
						glUniform3f(newShader->uLightOrigin,xyz.x,xyz.y,xyz.z);
					} else {
						matrix_c entityMatrixInv = entityMatrix.getInversed();
						vec3_c xyzLocal;
						entityMatrixInv.transformPoint(xyz,xyzLocal);
						glUniform3f(newShader->uLightOrigin,xyzLocal.x,xyzLocal.y,xyzLocal.z);
					}
				}
				if(newShader->uLightRadius != -1) {
					glUniform1f(newShader->uLightRadius,curLight->getRadius());
				}
			}
			if(newShader->uViewOrigin != -1) {
				glUniform3f(newShader->uViewOrigin,
					this->camOriginEntitySpace.x,
					this->camOriginEntitySpace.y,
					this->camOriginEntitySpace.z);
			}
		}
	}
	// temporary vertex buffer for stages that requires CPU 
	// vertex calculations, eg. texgen enviromental, etc.
	rVertexBuffer_c stageVerts;
	virtual void drawElements(const class rVertexBuffer_c &verts, const class rIndexBuffer_c &indices) {
		if(indices.getNumIndices() == 0)
			return;

		stopDrawingShadowVolumes();

		if(bDrawOnlyOnDepthBuffer) {
			glColorMask(false, false, false, false);
			bindShader(0);
			bindVertexBuffer(&verts);
			bindIBO(&indices);
			turnOffAlphaFunc();
			turnOffPolygonOffset();
			turnOffTextureMatrices();
			disableAllTextures();
			drawCurIBO();
			return;
		}
		glColorMask(true, true, true, true);

		if(rb_showNormalColors.getInt()) {
			glShader_c *sh = GL_RegisterShader("showNormalColors");
			if(sh) {
				turnOffAlphaFunc();
				turnOffPolygonOffset();
				turnOffTextureMatrices();
				disableAllTextures();
				bindShader(sh);
				bindVertexBuffer(&verts);
				bindIBO(&indices);
				drawCurIBO();
				return;
			}
		}

		if(lastMat) {
			glCull(lastMat->getCullType());
		} else {
			glCull(CT_FRONT_SIDED);
		}

		//bindVertexBuffer(&verts);
		bindIBO(&indices);

		if(lastMat) {
			if(lastMat->getPolygonOffset()) {
				this->setPolygonOffset(-1,-2);
			} else {
				this->turnOffPolygonOffset();
			}
			u32 numMatStages = lastMat->getNumStages();
			for(u32 i = 0; i < numMatStages; i++) {
				const mtrStageAPI_i *s = lastMat->getStage(i);
				enum stageType_e stageType = s->getStageType();
				setAlphaFunc(s->getAlphaFunc());	
				if(curLight == 0) {
					const blendDef_s &bd = s->getBlendDef();
					setBlendFunc(bd.src,bd.dst);
				} else {
					// light interactions are appended with addictive blending
					setBlendFunc(BM_ONE,BM_ONE);
				}
#if 1
				// test; it seems beam shader should be drawn with depthmask off..
				//if(!stricmp(lastMat->getName(),"textures/sfx/beam")) {
				//if(s->getBlendDef().isNonZero()) {
				// (only if not doing multipass lighting)
				if(curLight == 0) {
					if(s->getDepthWrite()==false) {
						setGLDepthMask(false);
					} else {
						setGLDepthMask(true);
					}
				} else {
					// lighting passes (and shadow volume drawcalls)
					// should be done with glDepthMask off
					setGLDepthMask(false);
				}
#endif
				if(s->hasTexMods()) {
					matrix_c mat;
					s->applyTexMods(mat,this->timeNowSeconds);
					this->setTextureMatrixCustom(0,mat);
				} else {
					this->setTextureMatrixIdentity(0);
				}
				bool bindLightmapCoordinatesToFirstTextureSlot = false;
				if(stageType == ST_COLORMAP_LIGHTMAPPED) {
					// draw multitextured surface with
					// - colormap at slot 0
					// - lightmap at slot 1
					textureAPI_i *t = s->getTexture(this->timeNowSeconds);
					bindTex(0,t->getInternalHandleU32());
					if(lastLightmap) {
						bindTex(1,lastLightmap->getInternalHandleU32());
					} else {
						bindTex(1,0);
					}
				} else if(stageType == ST_LIGHTMAP) {
					// bind lightmap to first texture slot.
					// draw ONLY lightmap
					if(lastLightmap) {
						bindTex(0,lastLightmap->getInternalHandleU32());
					} else {
						bindTex(0,0);
					}
					bindLightmapCoordinatesToFirstTextureSlot = true;
					unbindTex(1);
				} else {
					// draw colormap only
					textureAPI_i *t = s->getTexture(this->timeNowSeconds);
					bindTex(0,t->getInternalHandleU32());
					unbindTex(1);
				}
				// use given vertex buffer (with VBOs created) if we dont have to do any material calculations on CPU
				const rVertexBuffer_c *selectedVertexBuffer = &verts;

				// see if we have to modify current vertex array on CPU
				// TODO: do deforms in GLSL shader
				if(s->hasTexGen() && (this->gpuTexGensSupported()==false)) {
					// copy vertices data (first big CPU bottleneck)
					// (but only if we havent done this already)
					if(selectedVertexBuffer == &verts) {
						stageVerts = verts;
						selectedVertexBuffer = &stageVerts;
						if(rb_printMemcpyVertexArrayBottleneck.getInt()) {
							g_core->Print("RB_GL: Copying %i vertices to draw material %s\n",verts.size(),lastMat->getName());
						}
					}
					// apply texgen effect (new texcoords)
					if(s->getTexGen() == TCG_ENVIRONMENT) {
						if(indices.getNumIndices() < verts.size()) {
							// if there are more vertices than indexes, we're sure that some of verts are unreferenced,
							// so we dont have to calculate texcoords for EVERY ONE of them
							stageVerts.calcEnvironmentTexCoordsForReferencedVertices(indices,this->camOriginEntitySpace);
						} else {
							stageVerts.calcEnvironmentTexCoords(this->camOriginEntitySpace);
						}
					}
				}
				// by default dont use vertex colors...
				// (right now it overrides the setting from frontend)
				bindVertexColors = false;
				if(s->hasRGBGen()) {
					if(s->getRGBGenType() == RGBGEN_IDENTITY) {
						bindVertexColors = false;
					} else if(s->getRGBGenType() == RGBGEN_VERTEX) {
						// just use vertex colors from VBO,
						// nothing to calculate on CPU
						bindVertexColors = true;
					} else if(0) {
						bindVertexColors = true;
						// copy vertices data (first big CPU bottleneck)
						// (but only if we havent done this already)
						if(selectedVertexBuffer == &verts) {
							stageVerts = verts;
							selectedVertexBuffer = &stageVerts;
							if(rb_printMemcpyVertexArrayBottleneck.getInt()) {
								g_core->Print("Copying %i vertices to draw material %s\n",verts.size(),lastMat->getName());
							}
						}
						// apply rgbGen effect (new rgb colors)
						//bindStageColors = true; // FIXME, it's already set!!!
						enum rgbGen_e rgbGenType = s->getRGBGenType();
						if(rgbGenType == RGBGEN_CONST) {
							byteRGB_s col;
							vec3_c colFloats;
							s->getRGBGenConstantColor3f(colFloats);
							col.fromFloats(colFloats);
							stageVerts.setVertexColorsToConstValues(col);
							stageVerts.setVertexAlphaToConstValue(255);
						} else if(rgbGenType == RGBGEN_WAVE) {
							//float val = s->rgbGen.getWaveForm().evaluate();
							//byte valAsByte = val * 255.f;
							//stageVerts.setVertexColorsToConstValue(valAsByte);
							//stageVerts.setVertexAlphaToConstValue(255);
						} else if(rgbGenType == RGBGEN_VERTEX) {

						} else {

						}
					}
				} else {
					// if (rgbGen is not set) and (lightmap is not present) and (vertex colors are present)
					// -> enable drawing with vertex colors
					if(bHasVertexColors && (lastLightmap == 0) && (curLight == 0)) {
						// this is a fix for q3 static "md3" models 
						// (those loaded directly from .bsp file and not from .md3 file)
						bindVertexColors = true;
					}
				}
	
				bool modifiedVertexArrayOnCPU = (selectedVertexBuffer != &verts);

				// see if we have to bind a GLSL shader
				glShader_c *selectedShader = 0;
				if(curLight) {
					// TODO: add Q3 material effects handling to per pixel lighting GLSL shader....
					selectedShader = GL_RegisterShader("perPixelLighting");
					bindShader(selectedShader);
				} else if(lastMat->isPortalMaterial() == false &&
					(
					gl_alwaysUseGLSLShaders.getInt() ||
					(modifiedVertexArrayOnCPU == false && (s->hasTexGen() && this->gpuTexGensSupported()))
					)
					) {
					glslPermutationFlags_s glslShaderDesc;
					if(stageType == ST_COLORMAP_LIGHTMAPPED) {
						glslShaderDesc.hasLightmap = true;
					}
					if(bindVertexColors) {
						glslShaderDesc.hasVertexColors = true;
					}
					if(s->hasTexGen()) {
						glslShaderDesc.hasTexGenEnvironment = true;
					}

					selectedShader = GL_RegisterShader("genericShader",&glslShaderDesc);
					if(selectedShader) {
						bindShader(selectedShader);
					}
				} else {
					bindShader(0);
				}
				// draw the current material stage using selected vertex buffer.
				bindVertexBuffer(selectedVertexBuffer,bindLightmapCoordinatesToFirstTextureSlot);

				drawCurIBO();
				CHECK_GL_ERRORS;
			}
		} else {
			if(curLight == 0) {
				setBlendFunc(BM_NOT_SET,BM_NOT_SET);
			} else {
				// light interactions are appended with addictive blending
				setBlendFunc(BM_ONE,BM_ONE);
			}
			drawCurIBO();
		}
		if(rb_showTris.getInt()) {
			this->unbindMaterial();
			this->bindShader(0);
			glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
			if(rb_showTris.getInt()==1)
				setDepthRange( 0, 0 ); 
			drawCurIBO();	
			if(rb_showTris.getInt()==1)
				setDepthRange( 0, 1 ); 
			glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
		}
	}
	virtual void drawElementsWithSingleTexture(const class rVertexBuffer_c &verts, const class rIndexBuffer_c &indices, class textureAPI_i *tex) {
		if(tex == 0)
			return;

		this->glCull(CT_TWO_SIDED);

		stopDrawingShadowVolumes();

		disableAllTextures();
		bindVertexBuffer(&verts);
		bindIBO(&indices);			
		bindTex(0,tex->getInternalHandleU32());
		drawCurIBO();
	}
	void startDrawingShadowVolumes() {
		if(bDrawingShadowVolumes == true)
			return;
		disableAllTextures();
		disableNormalArray();
		disableTexCoordArrayForTexSlot(1);
		disableTexCoordArrayForTexSlot(0);
		unbindVertexBuffer();
		unbindMaterial();
        glClear(GL_STENCIL_BUFFER_BIT); // We clear the stencil buffer
        glDepthFunc(GL_LESS); // We change the z-testing function to LESS, to avoid little bugs in shadow
        glColorMask(false, false, false, false); // We dont draw it to the screen
        glStencilFunc(GL_ALWAYS, 0, 0); // We always draw whatever we have in the stencil buffer
		setGLDepthMask(false);
		setGLStencilTest(true);

		bDrawingShadowVolumes = true;
	}
	void stopDrawingShadowVolumes() {
		if(bDrawingShadowVolumes == false)
			return;

		// We draw our lighting now that we created the shadows area in the stencil buffer
        glDepthFunc(GL_LEQUAL); // we put it again to LESS or EQUAL (or else you will get some z-fighting)
		glCull(CT_FRONT_SIDED);// glCullFace(GL_FRONT);//BACK); // we draw the front face
        glColorMask(true, true, true, true); // We enable color buffer
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); // Drawing will not affect the stencil buffer
        glStencilFunc(GL_EQUAL, 0x0, 0xff); // And the most important thing, the stencil function. Drawing if equal to 0

		bDrawingShadowVolumes = false;
	}
	virtual void drawIndexedShadowVolume(const class rPointBuffer_c *points, const class rIndexBuffer_c *indices) {
		if(indices->getNumIndices() == 0)
			return;
		startDrawingShadowVolumes();

		bindIBO(indices);
		glVertexPointer(3,GL_FLOAT,sizeof(hashVec3_c),points->getArray());
		
		glCull(CT_BACK_SIDED);
		glStencilOp(GL_KEEP, GL_INCR, GL_KEEP); // increment if the depth test fails

		drawCurIBO(); // draw the shadow volume

		glCull(CT_FRONT_SIDED);
		glStencilOp(GL_KEEP, GL_DECR, GL_KEEP); // decrement if the depth test fails

		drawCurIBO(); // draw the shadow volume

		//glVertexPointer(3,GL_FLOAT,sizeof(hashVec3_c),0);
		//bindIBO(0);
	}
	virtual void beginFrame() {
		// NOTE: for stencil shadows, stencil buffer should be cleared here as well.
		if(1) {
		    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		} else {
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
		glClearColor(0,0,0,0);

		// reset counters
		c_frame_vbsReusedByDifferentDrawCall = 0;
	}
	virtual void endFrame() {
		if(gl_callGLFinish.getInt()) {
			glFinish();
		}
		g_sharedSDLAPI->endFrame();
	}	
	virtual void clearDepthBuffer() {
		if(bDepthMask == false) {
			glDepthMask(true);
		}
		// glClear(GL_DEPTH_BUFFER_BIT) doesnt work when glDepthMask is false...
		glClear(GL_DEPTH_BUFFER_BIT);
		if(bDepthMask == false) {
			glDepthMask(false);
		}
	}
	virtual void setup2DView() {
		setGLDepthMask(true);
		setGLStencilTest(false);
		disablePortalClipPlane();
		setIsMirror(false);

		glViewport(0,0,getWinWidth(),getWinHeight());
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();

		glOrtho (0, getWinWidth(), getWinHeight(), 0, 0, 1);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		
		// depth test is not needed while drawing 2d graphics
		glDisable(GL_DEPTH_TEST);
		// disable materials
		turnOffPolygonOffset();
		// rVertexBuffers are used only for 3d
		unbindVertexBuffer();
		bindIBO(0); // we're not using rIndexBuffer_c system for 2d graphics
	}
	matrix_c currentModelViewMatrix;
	void loadModelViewMatrix(const matrix_c &newMat) {
		//if(newMat.compare(currentModelViewMatrix)) 
		//	return;
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(newMat);
		currentModelViewMatrix = newMat;
	}
	virtual void setupWorldSpace() {
		resultMatrix = worldModelMatrix;

		this->loadModelViewMatrix(this->resultMatrix);

		camOriginEntitySpace = this->camOriginWorldSpace;

		usingWorldSpace = true;
	}
	virtual void setupEntitySpace(const axis_c &axis, const vec3_c &origin) {
		entityAxis = axis;
		entityOrigin = origin;

		entityMatrix.fromAxisAndOrigin(axis,origin);
		entityMatrixInverse = entityMatrix.getInversed();

		entityMatrixInverse.transformPoint(camOriginWorldSpace,camOriginEntitySpace);

		this->resultMatrix = this->worldModelMatrix * entityMatrix;

		this->loadModelViewMatrix(this->resultMatrix);

		usingWorldSpace = false;
	}
	virtual void setupEntitySpace2(const class vec3_c &angles, const class vec3_c &origin) {
		axis_c ax;
		ax.fromAngles(angles);
		setupEntitySpace(ax,origin);
	}
	virtual bool isUsingWorldSpace() const {
		return usingWorldSpace;
	}
	virtual const matrix_c &getEntitySpaceMatrix() const {
		// we dont need to call it when usingWorldSpace == true, 
		// just use identity matrix
		assert(usingWorldSpace == false);
		return entityMatrix;
	}
	virtual void setup3DView(const class vec3_c &newCamPos, const class axis_c &newCamAxis) {
		camOriginWorldSpace = newCamPos;
		viewAxis = newCamAxis;

		// transform by the camera placement and view axis
		this->worldModelMatrix.invFromAxisAndVector(newCamAxis,newCamPos);
		// convert to gl coord system
		this->worldModelMatrix.toGL();

		setupWorldSpace();

		glEnable(GL_DEPTH_TEST);
	}
	virtual void setupProjection3D(const projDef_s *pd) {
		matrix_c proj;
		//frustum.setup(fovX, fovY, zFar, axis, origin);
		proj.setupProjection(pd->fovX,pd->fovY,pd->zNear,pd->zFar);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glLoadMatrixf(proj);
	}
	virtual void drawCapsuleZ(const float *xyz, float h, float w) {
		this->bindIBO(0);
		this->unbindVertexBuffer();

		//glDisable(GL_DEPTH_TEST);
		glTranslatef(xyz[0],xyz[1],xyz[2]);
		// draw lower sphere (sliding on the ground)
		glTranslatef(0,0,-(h*0.5f));
		glutSolidSphere(w,12,12);
		glTranslatef(0,0,(h*0.5f));
		// draw upper sphere (player's "head")
		glTranslatef(0,0,(h*0.5f));
		glutSolidSphere(w,12,12);
		glTranslatef(0,0,-(h*0.5f));
		//glutSolidSphere(32,16,16);

			// draw 'body'
		glTranslatef(0,0,-(h*0.5f));
		GLUquadricObj *obj = gluNewQuadric();
		gluCylinder(obj, w, w, h, 30, 30);
		gluDeleteQuadric(obj);
		glTranslatef(0,0,(h*0.5f));
		glTranslatef(-xyz[0],-xyz[1],-xyz[2]);
	}
	virtual void drawBoxHalfSizes(const float *halfSizes) {
		this->bindIBO(0);
		this->unbindVertexBuffer();

		glutSolidCube(halfSizes[0]*2);
	}
	virtual void drawLineFromTo(const float *from, const float *to, const float *colorRGB) {
		this->bindIBO(0);
		this->unbindVertexBuffer();
		this->unbindMaterial();

		glBegin(GL_LINES);
		glColor3fv(colorRGB);
		glVertex3fv(from);
		glColor3fv(colorRGB);
		glVertex3fv(to);
		glEnd();
	}	
	virtual void drawBBLines(const class aabb &bb) {
		vec3_c verts[8];
		for(u32 i = 0; i < 8; i++) {
			verts[i] = bb.getPoint(i);
		}
		glBegin(GL_LINES);
		// mins.z
		glVertex3fv(verts[0]);
		glVertex3fv(verts[1]);
		glVertex3fv(verts[0]);
		glVertex3fv(verts[2]);
		glVertex3fv(verts[2]);
		glVertex3fv(verts[3]);
		glVertex3fv(verts[3]);
		glVertex3fv(verts[1]);
		// maxs.z
		glVertex3fv(verts[4]);
		glVertex3fv(verts[5]);
		glVertex3fv(verts[4]);
		glVertex3fv(verts[6]);
		glVertex3fv(verts[6]);
		glVertex3fv(verts[7]);
		glVertex3fv(verts[7]);
		glVertex3fv(verts[5]);
		// mins.z -> maxs.z
		glVertex3fv(verts[0]);
		glVertex3fv(verts[4]);
		glVertex3fv(verts[1]);
		glVertex3fv(verts[5]);
		glVertex3fv(verts[2]);
		glVertex3fv(verts[6]);
		glVertex3fv(verts[3]);
		glVertex3fv(verts[7]);
		glEnd();
	}        
	virtual bool areGPUOcclusionQueriesSupported() const {
		int bitsSupported = 0;
        glGetQueryiv(GL_SAMPLES_PASSED_ARB, GL_QUERY_COUNTER_BITS_ARB, &bitsSupported);
        if (bitsSupported == 0) {
           return false;
        }
		return true;
	}
	virtual class occlusionQueryAPI_i *allocOcclusionQuery() {
		if(areGPUOcclusionQueriesSupported() == false)
			return 0;
		glOcclusionQuery_c *ret = new glOcclusionQuery_c;
		ret->generateOcclusionQuery();
		return ret;
	}  
	virtual void setRenderTimeSeconds(float newCurTime) {
		this->timeNowSeconds = newCurTime;
	}
	virtual void setIsMirror(bool newBIsMirror) {
		if(newBIsMirror == this->isMirror)
			return;
		this->isMirror = newBIsMirror;
		// force cullType reset, because mirror views
		// must have CT_BACK with CT_FRONT swapped
		this->prevCullType = CT_NOT_SET;
	}
	virtual void setPortalClipPlane(const class plane_c &pl, bool bEnabled) {
		double plane2[4];
#if 0
		plane2[0] = viewAxis[0].dotProduct(pl.norm);
		plane2[1] = viewAxis[1].dotProduct(pl.norm);
		plane2[2] = viewAxis[2].dotProduct(pl.norm);
		plane2[3] = pl.norm.dotProduct(camOriginWorldSpace) - pl.dist;
#else
		plane2[0] = -pl.norm.x;
		plane2[1] = -pl.norm.y;
		plane2[2] = -pl.norm.z;
		plane2[3] = -pl.dist;
#endif
		if(bEnabled) {
			glClipPlane (GL_CLIP_PLANE0, plane2);
			glEnable (GL_CLIP_PLANE0);
		} else {
			glDisable (GL_CLIP_PLANE0);	
		}
	}
	virtual void disablePortalClipPlane() {
		glDisable (GL_CLIP_PLANE0);	
	}
	virtual void init()  {
		if(backendInitialized) {
			g_core->Error(ERR_DROP,"rbSDLOpenGL_c::init: already initialized\n");
			return;		
		}
		// cvars
		AUTOCVAR_RegisterAutoCvars();

		// init SDL window
		g_sharedSDLAPI->init();

		u32 res = glewInit();
		if (GLEW_OK != res) {
			g_core->Error(ERR_DROP,"rbSDLOpenGL_c::init: glewInit() failed. Cannot init openGL renderer\n");
			return;
		}
		// ensure that all the states are reset after vid_restart
		memset(texStates,0,sizeof(texStates));
		curTexSlot = -1;
		highestTCSlotUsed = 0;
		// materials
		lastMat = 0;
		lastLightmap = 0;
		bindVertexColors = 0;
		boundVBO = 0;
		boundIBO = 0;
		boundGPUIBO = false;
		blendSrc = -2;
		blendDst = -2;
		blendEnable = false;
		curLight = 0;
		bDrawOnlyOnDepthBuffer = false;
		bDrawingShadowVolumes = false;
		bDepthMask = true;
		prevCullType = CT_NOT_SET;
		stencilTestEnabled = 0;
		//glShadeModel( GL_SMOOTH );
		glDepthFunc( GL_LEQUAL );
		glEnableClientState(GL_VERTEX_ARRAY);

		// THE hack below is not needed now,
		// lightmapped surfaces were too dark because
		// they were drawn with vertex colors enabled
		// Right now, when I have added basic 
		// "rgbGen" Q3 shader keyword handling,
		// vertex colors are enabled only
		// when they are really needed...
#if 0
		selectTex(1);
		// increase lightmap brightness. They are very dark when vertex colors are enabled.
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);

		//	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_ADD);

		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);

		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
		glTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_PREVIOUS_ARB);
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 4.0f);
		selectTex(0);
#endif
		backendInitialized = true;
	}
	virtual void shutdown(bool destroyWindow)  {
		if(backendInitialized == false) {
			g_core->Error(ERR_DROP,"rbSDLOpenGL_c::shutdown: already shutdown\n");
			return;		
		}
		AUTOCVAR_UnregisterAutoCvars();
		lastMat = 0;
		lastLightmap = 0;
		if(destroyWindow) {
			g_sharedSDLAPI->shutdown();
		}
		backendInitialized = false;
	}
	virtual u32 getWinWidth() const {
		//return glConfig.vidWidth;
		return g_sharedSDLAPI->getWinWidth();
	}
	virtual u32 getWinHeight() const {
		//return glConfig.vidHeight;
		return g_sharedSDLAPI->getWinHeigth();
	}
	virtual void uploadTextureRGBA(class textureAPI_i *out, const byte *data, u32 w, u32 h) {
		out->setWidth(w);
		out->setHeight(h);
		u32 texID;
		glGenTextures(1, &texID);
		glBindTexture(GL_TEXTURE_2D, texID);
#if 0
		// wtf it doesnt work?
		glTexImage2D(GL_TEXTURE_2D, 0, 4, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
#else
		gluBuild2DMipmaps(GL_TEXTURE_2D,GL_RGBA,w,h,GL_RGBA,GL_UNSIGNED_BYTE, data);
#endif
		CHECK_GL_ERRORS;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		if(out->getBClampToEdge()) {
			// this is used for skyboxes 
			// (without it they are shown with strange artifacts at texture edges)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
		} else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); 
		}
		//glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		CHECK_GL_ERRORS;
		glBindTexture(GL_TEXTURE_2D, 0);	
		out->setInternalHandleU32(texID);
	}
	virtual void freeTextureData(class textureAPI_i *tex) {
		u32 handle = tex->getInternalHandleU32();
		glDeleteTextures(1,&handle);
		tex->setInternalHandleU32(0);
	}
	virtual void uploadLightmapRGB(class textureAPI_i *out, const byte *data, u32 w, u32 h) {
		out->setWidth(w);
		out->setHeight(h);
		u32 texID;
		glGenTextures(1, &texID);
		glBindTexture(GL_TEXTURE_2D, texID);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);	
		CHECK_GL_ERRORS;
		glBindTexture(GL_TEXTURE_2D, 0);
		out->setInternalHandleU32(texID);	
	}
	virtual bool createVBO(class rVertexBuffer_c *vbo) {
		if(vbo->getInternalHandleU32()) {
			destroyVBO(vbo);
		}
		if(glGenBuffers == 0)
			return true; // VBO not supported
		u32 h;
		glGenBuffers(1,&h);
		vbo->setInternalHandleU32(h);
		glBindBuffer(GL_ARRAY_BUFFER, h);
		glBufferData(GL_ARRAY_BUFFER, sizeof(rVert_c)*vbo->size(), vbo->getArray(), GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		return false; // ok
	}
	virtual bool destroyVBO(class rVertexBuffer_c *vbo) {
		if(vbo == 0)
			return true; // NULL ptr
		if(glDeleteBuffers == 0)
			return true; // no VBO support
		u32 h = vbo->getInternalHandleU32();
		if(h == 0)
			return true; // buffer wasnt uploaded to gpu
		glDeleteBuffers(1,&h);
		vbo->setInternalHandleU32(0);
		return false; // ok
	}
	virtual bool createIBO(class rIndexBuffer_c *ibo) {
		if(ibo->getInternalHandleU32()) {
			destroyIBO(ibo);
		}
		if(glGenBuffers == 0)
			return true; // VBO not supported
		u32 h;
		glGenBuffers(1,&h);
		ibo->setInternalHandleU32(h);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, h);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibo->getSizeInBytes(), ibo->getArray(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		return false; // ok		
	}
	virtual bool destroyIBO(class rIndexBuffer_c *ibo) {
		if(ibo == 0)
			return true; // NULL ptr
		if(glDeleteBuffers == 0)
			return true; // no VBO support
		u32 h = ibo->getInternalHandleU32();
		if(h == 0)
			return true; // buffer wasnt uploaded to gpu
		glDeleteBuffers(1,&h);
		ibo->setInternalHandleU32(0);
		return false; // ok
	}
};

static rbSDLOpenGL_c g_staticSDLOpenGLBackend;

glOcclusionQuery_c::glOcclusionQuery_c() {
	oqHandle = 0;
	lastResult = 0xffffffff;
	waitingForResult = false;
}
glOcclusionQuery_c::~glOcclusionQuery_c() {

}
void glOcclusionQuery_c::generateOcclusionQuery() {
	glGenQueriesARB(1, &oqHandle);
}
void glOcclusionQuery_c::assignSphereQuery(const vec3_c &p, float radius) {
	// stop drawing shadow volumes
	g_staticSDLOpenGLBackend.stopDrawingShadowVolumes();
	glColorMask(false,false,false,false);
	g_staticSDLOpenGLBackend.setGLDepthMask(false);
	g_staticSDLOpenGLBackend.glCull(CT_TWO_SIDED);
	glBeginQueryARB(GL_SAMPLES_PASSED_ARB, oqHandle);
	glTranslatef(p.x,p.y,p.z);
	glutSolidSphere(radius, 20, 20);
	glTranslatef(-p.x,-p.y,-p.z);
	glEndQueryARB(GL_SAMPLES_PASSED_ARB);
	g_staticSDLOpenGLBackend.setGLDepthMask(true);
	glColorMask(true,true,true,true);
	waitingForResult = true;
}
u32 glOcclusionQuery_c::getNumSamplesPassed() const {
	if(waitingForResult == false) {
		return this->lastResult;
	}
	u32 resultSamples;
	glGetQueryObjectuivARB(oqHandle, GL_QUERY_RESULT_ARB, &resultSamples);
	this->lastResult = resultSamples;
	return resultSamples;
}
bool glOcclusionQuery_c::isResultAvailable() const {
	u32 bAvail;
	glGetQueryObjectuivARB(oqHandle, GL_QUERY_RESULT_AVAILABLE_ARB, &bAvail);
	if(bAvail)
		return true;
	return false;
}
u32 glOcclusionQuery_c::waitForLatestResult() const {
	u32 bAvail;
	while(1) {
		glGetQueryObjectuivARB(oqHandle, GL_QUERY_RESULT_AVAILABLE_ARB, &bAvail);
		if(bAvail) {
			break;
		}
	}
	return getNumSamplesPassed();
}
u32 glOcclusionQuery_c::getPreviousResult() const {
	return lastResult;
}


void SDLOpenGL_RegisterBackEnd() {
	g_iFaceMan->registerInterface((iFaceBase_i *)(void*)&g_staticSDLOpenGLBackend,RENDERER_BACKEND_API_IDENTSTR);
}