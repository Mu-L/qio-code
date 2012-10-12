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
// rf_surface.h
#ifndef __RF_SURFACE_H__
#define __RF_SURFACE_H__

#include "../rIndexBuffer.h"
#include "../rVertexBuffer.h"
#include <shared/str.h>
#include <math/aabb.h>

class r_surface_c {
	str name;
	str matName;
	class mtrAPI_i *mat;
	class textureAPI_i *lightmap;
	rVertexBuffer_c verts;
	rIndexBuffer_c indices;
	aabb bounds;
public:
	r_surface_c() {
		mat = 0;
	}
	u32 getNumVerts() const {
		return verts.size();
	}
	void addVert(const rVert_c &v) {
		verts.push_back(v);
	}
	void addIndex(u32 idx) {
		indices.addIndex(idx);
	}
	void clear() {
		indices.destroy();
		verts.destroy();
	}
	void setMaterial(mtrAPI_i *newMat) {
		mat = newMat;
	}
	void setLightmap(textureAPI_i *newLM) {
		lightmap = newLM;
	}

	void recalcBB() {
		bounds.clear();
		for(u32 i = 0; i < verts.size(); i++) {
			bounds.addPoint(verts[i].xyz);
		}
	}
	void drawSurface();

	void traceRay(class trace_c &tr);


};

#endif // __RF_SURFACE_H__

