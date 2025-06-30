
#include <math.h>
#include <stdio.h>

#include "../../include/druid.h"


inline Vec2 v2Add(Vec2 a, Vec2 b)
{
    return (Vec2){ a.x + b.x, a.y + b.y };
}


inline Vec2 v2Sub(Vec2 a, Vec2 b)
{
    return (Vec2){ a.x - b.x, a.y - b.y };
}


inline Vec2 v2Scale(Vec2 a, float b)
{
    return (Vec2){a.x * b, a.y * b };
}


inline Vec2 v2Mul(Vec2 a, Vec2 b)
{
    return (Vec2){ a.x * b.x, a.y * b.y };
}
inline float v2Mag(Vec2 a)
{
    return (f32)sqrt(a.x * a.x + a.y * a.y);
}

inline float v2Dis(Vec2 a, Vec2 b)
{
    return  (f32)(v2Mag(v2Sub(b, a)));
}

inline Vec2i v2Tov2i(Vec2 a)
{
    return (Vec2i){(int)a.x,(int)a.y};
}

inline Vec2 v2iTov2(Vec2i a)
{
    return (Vec2){(float)a.x, (float)a.y};
}



inline Vec2 v2Div(Vec2 a, float b)
{
	return (Vec2){(float)(a.x / b),(float)(a.y / b)};
}


inline bool v2Equal(Vec2 a, Vec2 b)
{
	return a.x == b.x && a.y == b.y;
}

inline Vec3 v3Add(Vec3 a, Vec3 b)
{
	return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 v3Sub(Vec3 a, Vec3 b)
{
	return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 v3Scale(Vec3 a, float b)
{

	return (Vec3){a.x * b, a.y * b, a.z * b};
}

inline Vec3 v3Mul(Vec3 a, Vec3 b)
{	
	return (Vec3){a.x * b.x, a.y * b.y, a.z * b.z};
}

inline float v3Mag(Vec3 a)
{
	return (f32)sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

inline float v3Dis(Vec3 a, Vec3 b)
{
	return (f32)v3Mag(v3Sub(b,a));
}

inline Vec3i v3Tov3i(Vec3 a)
{
	return (Vec3i){(u32)a.x, (u32)a.y,(u32)a.z};
}

inline Vec3 v3iTov3(Vec3i a)
{
	return (Vec3){(f32)a.x, (f32)a.y,(f32)a.z};
}

inline Vec3 v3Div(Vec3 a, float b)
{
	return (Vec3){a.x/b,a.y/b,a.z/b};
}

inline Vec3 v3Norm(Vec3 a)
{
    	float mag = v3Mag(a);
    	Vec3 output = (mag > 0.00001f) ? v3Div(a, mag) : (Vec3){0, 0, 0};
	return output;
}


inline bool v3Equal(Vec3 a, Vec3 b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}
inline Vec3 v3Cross(Vec3 a, Vec3 b)
{
	return (Vec3){
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline f32 v3Dot(Vec3 a, Vec3 b)
{
    return (f32)a.x * b.x + a.y * b.y + a.z * b.z;
}


inline void matAdd(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] + b[x][y];
		}
	}

}
inline void matSub(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] - b[x][y];
		}
	}

}


inline void matDiv(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] / b[x][y];
		}
	}

}
inline void matMul(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] * b[x][y];
		}
	}

}

inline void matScale(f32** a,f32 b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] * b;
		}
	}

}

f32** matCreate(Vec2i size)
{
    f32** mat = (f32**)malloc(sizeof(f32*) * size.x);
    for(u32 i = 0; i < size.x; i++)
    {
        mat[i] = (f32*)malloc(sizeof(f32) * size.y);
    }
    return mat;
}

void freeMat(f32** mat, Vec2i size)
{
    for(u32 i = 0; i < size.x; i++)
    {
        free(mat[i]);
    }
    free(mat);
}


inline Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ) 
{
    f32 f = 1.0f / tanf(fovRadians / 2.0f);
    f32 nf = 1.0f / (nearZ - farZ);

    Mat4 result = {0}; // zero-init

    result.m[0][0] = f / aspect;
    result.m[1][1] = f;
    result.m[2][2] = (farZ + nearZ) * nf;
    result.m[2][3] = -1.0f;
    result.m[3][2] = (2.0f * farZ * nearZ) * nf;

    return result;
}

inline Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = v3Norm(v3Sub(center, eye));
    Vec3 s = v3Norm(v3Cross(f, up));
    Vec3 u = v3Cross(s, f);

    Mat4 result = mat4Identity();

    // Row-major layout:
    result.m[0][0] = s.x;
    result.m[0][1] = s.y;
    result.m[0][2] = s.z;
    result.m[0][3] = -v3Dot(s, eye);

    result.m[1][0] = u.x;
    result.m[1][1] = u.y;
    result.m[1][2] = u.z;
    result.m[1][3] = -v3Dot(u, eye);

    result.m[2][0] = -f.x;
    result.m[2][1] = -f.y;
    result.m[2][2] = -f.z;
    result.m[2][3] = v3Dot(f, eye);

    result.m[3][0] = 0.0f;
    result.m[3][1] = 0.0f;
    result.m[3][2] = 0.0f;
    result.m[3][3] = 1.0f;

    return result;
}

//Create an identity matrix
inline Mat4 mat4Identity() 
{
    Mat4 result = {0};
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++) 
        {
            result.m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    return result;
}

//Create a zero matrix
inline Mat4 mat4Zero() 
{
    Mat4 result;
    for (int i = 0; i < 4; i++) 
    {
        for (int j = 0; j < 4; j++)
        {
            result.m[i][j] = 0.0f;
        }
    }
    return result;
}

//Create a translation matrix
inline Mat4 mat4Translate(Mat4 in, Vec3 translation)
{
    Mat4 out = in;

    out.m[0][3] += in.m[0][0] * translation.x + in.m[0][1] * translation.y + in.m[0][2] * translation.z;
    out.m[1][3] += in.m[1][0] * translation.x + in.m[1][1] * translation.y + in.m[1][2] * translation.z;
    out.m[2][3] += in.m[2][0] * translation.x + in.m[2][1] * translation.y + in.m[2][2] * translation.z;
    out.m[3][3] += in.m[3][0] * translation.x + in.m[3][1] * translation.y + in.m[3][2] * translation.z;
	/*
	    Mat4 result = mat4Identity();
    result.m[0][3] = translation.x;
    result.m[1][3] = translation.y;
    result.m[2][3] = translation.z;
    return result;
*/
    return out;
}


//Create a scale matrix
inline Mat4 mat4Scale(float scale) 
{
    Mat4 result = mat4Zero();
    result.m[0][0] = scale;
    result.m[1][1] = scale;
    result.m[2][2] = scale;
    result.m[3][3] = 1.0f;
    return result;
}

//Create a scale matrix with different scales per axis

inline Mat4 mat4ScaleVec(Vec3 scale) 
{
    Mat4 result = mat4Zero();
    result.m[0][0] = scale.x;
    result.m[1][1] = scale.y;
    result.m[2][2] = scale.z;
    result.m[3][3] = 1.0f;
    return result;
}

//Create rotation matrix around X axis
inline Mat4 mat4RotateX(float angleRadians) 
{
    Mat4 result = mat4Identity();
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    
    result.m[1][1] = c;
    result.m[1][2] = -s;
    result.m[2][1] = s;
    result.m[2][2] = c;
    
    return result;
}

//Create rotation matrix around Y axis
inline Mat4 mat4RotateY(float angleRadians) 
{
    Mat4 result = mat4Identity();
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    
    result.m[0][0] = c;
    result.m[0][2] = s;
    result.m[2][0] = -s;
    result.m[2][2] = c;
    
    return result;
}

//Create rotation matrix around Z axis
inline Mat4 mat4RotateZ(float angleRadians) 
{
    Mat4 result = mat4Identity();
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    
    result.m[0][0] = c;
    result.m[0][1] = -s;
    result.m[1][0] = s;
    result.m[1][1] = c;
    
    return result;
}

//Create rotation matrix for arbitrary axis
inline Mat4 mat4Rotate(float angleRadians, Vec3 axis) 
{
    Mat4 result = mat4Identity();
    
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    float t = 1.0f - c;
    
    //Normalize axis
    float len = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len > 0.0001f) 
    {
        axis.x /= len;
        axis.y /= len;
        axis.z /= len;
    }
    
    result.m[0][0] = t * axis.x * axis.x + c;
    result.m[0][1] = t * axis.x * axis.y - s * axis.z;
    result.m[0][2] = t * axis.x * axis.z + s * axis.y;
    
    result.m[1][0] = t * axis.x * axis.y + s * axis.z;
    result.m[1][1] = t * axis.y * axis.y + c;
    result.m[1][2] = t * axis.y * axis.z - s * axis.x;
    
    result.m[2][0] = t * axis.x * axis.z - s * axis.y;
    result.m[2][1] = t * axis.y * axis.z + s * axis.x;
    result.m[2][2] = t * axis.z * axis.z + c;
    
    return result;
}

//Matrix multiplication
inline Mat4 mat4Mul(Mat4 a, Mat4 b)
{

    Mat4 result = mat4Zero();
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int i = 0; i < 4; ++i) {
                result.m[row][col] += a.m[row][i] * b.m[i][col];
            }
        }
    }
    return result;

}



//Matrix addition
inline Mat4 mat4Add(Mat4 a, Mat4 b)
{
    Mat4 result;
    
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            result.m[i][j] = a.m[i][j] + b.m[i][j];
        }
    }
    
    return result;
}

//Matrix subtraction
inline Mat4 mat4Sub(Mat4 a, Mat4 b)
{
    Mat4 result;
    
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            result.m[i][j] = a.m[i][j] - b.m[i][j];
        }
    }
    
    return result;
}

//Scale all elements of a matrix
inline Mat4 mat4ScaleVal(Mat4 a, float scale)
{
    Mat4 result;
    
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            result.m[i][j] = a.m[i][j] * scale;
        }
    }
    
    return result;
}


Mat3 mat4ToMat3(const Mat4 m4) 
{
    Mat3 m3;
    for (int i = 0; i < 3; i++) 
    {
        for (int j = 0; j < 3; j++) 
        {
            m3.m[i][j] = m4.m[i][j];
        }
    }
    return m3;
}

Mat4 mat3ToMat4(const Mat3 m3) 
{
    Mat4 m4 = mat4Identity();
    for (int i = 0; i < 3; i++) 
    {
        for (int j = 0; j < 3; j++) 
        {
            m4.m[i][j] = m3.m[i][j];
        }
    }
    //Last row/column should be [0,0,0,1] for pure rotation
    m4.m[3][0] = m4.m[3][1] = m4.m[3][2] = 0.0f;
    m4.m[0][3] = m4.m[1][3] = m4.m[2][3] = 0.0f;
    m4.m[3][3] = 1.0f;
    return m4;
}


//Transform a vector by a matrix
inline Vec4 Mat4TransformVec4(Mat4 m, Vec4 v)
{
    Vec4 result;
    
    result.x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w;
    result.y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w;
    result.z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w;
    result.w = m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w;
    
    return result;
}

inline Vec3 Mat4TransformPoint(Mat4 m, Vec3 p) 
{
    Vec4 temp;
    
    temp.x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
    temp.y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
    temp.z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
    temp.w = m.m[3][0] * p.x + m.m[3][1] * p.y + m.m[3][2] * p.z + m.m[3][3];
    
    if (fabs(temp.w) > 0.0001f)
    {
        return (Vec3){temp.x / temp.w, temp.y / temp.w, temp.z / temp.w};
    }
    
    return (Vec3){temp.x, temp.y, temp.z};
}

inline Vec3 Mat4TransformDirection(Mat4 m, Vec3 d) 
{
    Vec3 result;
    
    result.x = m.m[0][0] * d.x + m.m[0][1] * d.y + m.m[0][2] * d.z;
    result.y = m.m[1][0] * d.x + m.m[1][1] * d.y + m.m[1][2] * d.z;
    result.z = m.m[2][0] * d.x + m.m[2][1] * d.y + m.m[2][2] * d.z;
    
    return result;
}

inline float Mat4Determinant(Mat4 m) 
{
    //Calculate the cofactors of the first row
    float c00 = m.m[1][1] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
               m.m[1][2] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) +
               m.m[1][3] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]);
    
    float c01 = m.m[1][0] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
               m.m[1][2] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
               m.m[1][3] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]);
    
    float c02 = m.m[1][0] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) -
               m.m[1][1] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
               m.m[1][3] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]);
    
    float c03 = m.m[1][0] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]) -
               m.m[1][1] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]) +
               m.m[1][2] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]);
    
    //Calculate determinant using cofactors of first row
    return m.m[0][0] * c00 - m.m[0][1] * c01 + m.m[0][2] * c02 - m.m[0][3] * c03;
}

//Invert a 4x4 matrix
inline Mat4 mat4Inverse(Mat4 m)
{
    float det = Mat4Determinant(m);
    
    if (fabs(det) < 0.0001f)
    {
        return mat4Identity();
    }
    
    float invDet = 1.0f / det;
    Mat4 result;
    
    //Calculate the adjugate matrix (transpose of cofactor matrix)
    result.m[0][0] = invDet * (
        m.m[1][1] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
        m.m[1][2] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) +
        m.m[1][3] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1])
    );
    
    result.m[0][1] = -invDet * (
        m.m[0][1] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
        m.m[0][2] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) +
        m.m[0][3] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1])
    );
    
    result.m[0][2] = invDet * (
        m.m[0][1] * (m.m[1][2] * m.m[3][3] - m.m[1][3] * m.m[3][2]) -
        m.m[0][2] * (m.m[1][1] * m.m[3][3] - m.m[1][3] * m.m[3][1]) +
        m.m[0][3] * (m.m[1][1] * m.m[3][2] - m.m[1][2] * m.m[3][1])
    );
    
    result.m[0][3] = -invDet * (
        m.m[0][1] * (m.m[1][2] * m.m[2][3] - m.m[1][3] * m.m[2][2]) -
        m.m[0][2] * (m.m[1][1] * m.m[2][3] - m.m[1][3] * m.m[2][1]) +
        m.m[0][3] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1])
    );
    
    result.m[1][0] = -invDet * (
        m.m[1][0] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
        m.m[1][2] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
        m.m[1][3] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0])
    );
    
    result.m[1][1] = invDet * (
        m.m[0][0] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) -
        m.m[0][2] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
        m.m[0][3] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0])
    );
    
    result.m[1][2] = -invDet * (
        m.m[0][0] * (m.m[1][2] * m.m[3][3] - m.m[1][3] * m.m[3][2]) -
        m.m[0][2] * (m.m[1][0] * m.m[3][3] - m.m[1][3] * m.m[3][0]) +
        m.m[0][3] * (m.m[1][0] * m.m[3][2] - m.m[1][2] * m.m[3][0])
    );
    
    result.m[1][3] = invDet * (
        m.m[0][0] * (m.m[1][2] * m.m[2][3] - m.m[1][3] * m.m[2][2]) -
        m.m[0][2] * (m.m[1][0] * m.m[2][3] - m.m[1][3] * m.m[2][0]) +
        m.m[0][3] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
    );
    
    result.m[2][0] = invDet * (
        m.m[1][0] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) -
        m.m[1][1] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
        m.m[1][3] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0])
    );
    
    result.m[2][1] = -invDet * (
        m.m[0][0] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) -
        m.m[0][1] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) +
        m.m[0][3] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0])
    );
    
    result.m[2][2] = invDet * (
        m.m[0][0] * (m.m[1][1] * m.m[3][3] - m.m[1][3] * m.m[3][1]) -
        m.m[0][1] * (m.m[1][0] * m.m[3][3] - m.m[1][3] * m.m[3][0]) +
        m.m[0][3] * (m.m[1][0] * m.m[3][1] - m.m[1][1] * m.m[3][0])
    );
    
    result.m[2][3] = -invDet * (
        m.m[0][0] * (m.m[1][1] * m.m[2][3] - m.m[1][3] * m.m[2][1]) -
        m.m[0][1] * (m.m[1][0] * m.m[2][3] - m.m[1][3] * m.m[2][0]) +
        m.m[0][3] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0])
    );
    
    result.m[3][0] = -invDet * (
        m.m[1][0] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]) -
        m.m[1][1] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]) +
        m.m[1][2] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0])
    );
    
    result.m[3][1] = invDet * (
        m.m[0][0] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]) -
        m.m[0][1] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]) +
        m.m[0][2] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0])
    );
    
    result.m[3][2] = -invDet * (
        m.m[0][0] * (m.m[1][1] * m.m[3][2] - m.m[1][2] * m.m[3][1]) -
        m.m[0][1] * (m.m[1][0] * m.m[3][2] - m.m[1][2] * m.m[3][0]) +
        m.m[0][2] * (m.m[1][0] * m.m[3][1] - m.m[1][1] * m.m[3][0])
    );
    
    result.m[3][3] = invDet * (
        m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
        m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
        m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0])
    );
    
    return result;
}

//Calculate the transpose of a matrix
inline Mat4 mat4Transpose(Mat4 m) 
{
    Mat4 result;
    
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++) 
        {
            result.m[i][j] = m.m[j][i];
        }
    }
    
    return result;
}


//Copy matrix to f32** format (for compatibility with existing code)
inline void mat4ToPointers(Mat4 mat, f32** dest) 
{
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            dest[i][j] = mat.m[i][j];
        }
    }
}

//Convert f32** to Mat4 format
inline Mat4 pointersToMat4(f32** src) 
{
    Mat4 result;
    for (int i = 0; i < 4; i++) 
    {
        for (int j = 0; j < 4; j++) 
        {
            result.m[i][j] = src[i][j];
        }
    }
    return result;
}



f32 clamp(f32 value, f32 minVal, f32 maxVal)
{
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

f32 degrees(f32 radians)
{
    return radians * (180.0f / PI);
}
//Helper quaternion functions you'll need
Vec4 quatIdentity()
{
    return (Vec4){0.0f, 0.0f, 0.0f, 1.0f};
}

Vec4 quatFromAxisAngle(Vec3 axis, f32 angle) 
    {
    //Check for zero-length axis first (safe, avoids NaN)
    f32 axisLength = v3Mag(axis);
    const f32 EPSILON = 1e-6f;  // Small threshold for floating-point precision

    if (axisLength < EPSILON)
    {
        return (Vec4){ 0.0f, 0.0f, 0.0f, 1.0f };  // Identity quaternion
    }

    //Normalize the axis (safe since length > EPSILON)
    Vec3 normalizedAxis = {
        axis.x / axisLength,
        axis.y / axisLength,
        axis.z / axisLength
    };

    f32 halfAngle = angle * 0.5f;
    f32 sinHalfAngle = sinf(halfAngle);
    
    return (Vec4){
        normalizedAxis.x * sinHalfAngle,
        normalizedAxis.y * sinHalfAngle,
        normalizedAxis.z * sinHalfAngle,
        cosf(halfAngle)
    };
}

Vec4 quatMul(Vec4 q1, Vec4 q2)
{
    Vec4 result;
    result.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
    result.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
    result.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
    result.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
    return result;
}

inline Vec4 quatNormalize(Vec4 q)
{
    f32 length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length > 0.0001f)
    {
        return (Vec4){q.x / length, q.y / length, q.z / length, q.w / length};
    }
    return quatIdentity();
}

//Transform a vector by a quaternion
inline Vec3 quatRotateVec3(Vec4 q, Vec3 v)
{
    //Create a pure quaternion from the vector
    Vec4 qv = {v.x, v.y, v.z, 0.0f};
    
    //q * v * q^-1 (using quaternion rotation formula)
    //For a unit quaternion, q^-1 = conjugate(q) = (-x, -y, -z, w)
    Vec4 qInv = {-q.x, -q.y, -q.z, q.w};
    Vec4 result = quatMul(quatMul(q, qv), qInv);
    
    return (Vec3){result.x, result.y, result.z};
}

//Convert quaternion to rotation matrix
inline Mat4 quatToRotationMatrix(Vec4 q)
{
    Mat4 result = mat4Identity();
    
    f32 xx = q.x * q.x;
    f32 xy = q.x * q.y;
    f32 xz = q.x * q.z;
    f32 xw = q.x * q.w;
    f32 yy = q.y * q.y;
    f32 yz = q.y * q.z;
    f32 yw = q.y * q.w;
    f32 zz = q.z * q.z;
    f32 zw = q.z * q.w;
    
    result.m[0][0] = 1.0f - 2.0f * (yy + zz);
    result.m[0][1] = 2.0f * (xy - zw);
    result.m[0][2] = 2.0f * (xz + yw);
    
    result.m[1][0] = 2.0f * (xy + zw);
    result.m[1][1] = 1.0f - 2.0f * (xx + zz);
    result.m[1][2] = 2.0f * (yz - xw);
    
    result.m[2][0] = 2.0f * (xz - yw);
    result.m[2][1] = 2.0f * (yz + xw);
    result.m[2][2] = 1.0f - 2.0f * (xx + yy);
    
    return result;
}

inline f32 radians(f32 degrees)
{
    return degrees * (PI / 180.0f);
}

inline Vec3 quatTransform(Vec4 q, Vec3 v)
{
	Vec4 qv = { v.x, v.y, v.z, 0.0f };
    Vec4 qInv = quatConjugate(q); // assuming q is normalized
    Vec4 result = quatMul(quatMul(q, qv), qInv);

    return (Vec3){ result.x, result.y, result.z };
}

inline Vec4 quatConjugate(const Vec4 q)
{
    return (Vec4){ -q.x, -q.y, -q.z, q.w };
}

inline f32 lerp(f32 a, f32 b, f32 t)
{
    return a + t * (b - a);
}
