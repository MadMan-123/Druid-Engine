
#include <cmath>
#include <stdio.h>

#include "../../include/druid.h"
//glm for perspective and lookat
#include <glm/glm.hpp>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

void glmMat4ToMat4(const glm::mat4& glmMat, Mat4* mat)
{	
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            mat->m[col][row] = glmMat[col][row];  // column-major copy
        }
    }
}


inline Vec2 v2Add(Vec2 a, Vec2 b)
{
    return { a.x + b.x, a.y + b.y };
}


inline Vec2 v2Sub(Vec2 a, Vec2 b)
{
    return { a.x - b.x, a.y - b.y };
}


inline Vec2 v2Scale(Vec2 a, float b)
{
    return {a.x * b, a.y * b };
}


inline Vec2 v2Mul(Vec2 a, Vec2 b)
{
    return { a.x * b.x, a.y * b.y };
}
inline float v2Mag(Vec2 a)
{
    return sqrt(a.x * a.x + a.y * a.y);
}

inline float v2Dis(Vec2 a, Vec2 b)
{
    return  (v2Mag(v2Sub(b, a)));
}

inline Vec2i v2Tov2i(Vec2 a)
{
    return {(int)a.x,(int)a.y};
}

inline Vec2 v2iTov2(Vec2i a)
{
    return {(float)a.x, (float)a.y};
}



inline Vec2 v2Div(Vec2 a, float b)
{
	return {(float)(a.x / b),(float)(a.y / b)};
}


inline bool v2Equal(Vec2 a, Vec2 b)
{
	return a.x == b.x && a.y == b.y;
}

inline Vec3 v3Add(Vec3 a, Vec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 v3Sub(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 v3Scale(Vec3 a, float b)
{

	return {a.x / b, a.y / b, a.z / b};
}

inline Vec3 v3Mul(Vec3 a, Vec3 b)
{	
	return {a.x * b.x, a.y * b.y, a.z * b.z};
}

inline float v3Mag(Vec3 a)
{
	return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

inline float v3Dis(Vec3 a, Vec3 b)
{
	return v3Mag(v3Sub(b,a));
}

inline Vec3i v3Tov3i(Vec3 a)
{
	return {(u32)a.x, (u32)a.y,(u32)a.z};
}

inline Vec3 v3iTov3(Vec3i a)
{
	return {(f32)a.x, (f32)a.y,(f32)a.z};
}

inline Vec3 v3Div(Vec3 a, float b)
{
	return {a.x/b,a.y/b,a.z/b};
}

inline Vec3 v3Norm(Vec3 a)
{
    float mag = v3Mag(a);
    return (mag > 0.00001f) ? v3Div(a, mag) : Vec3{0, 0, 0};
}


inline bool v3Equal(Vec3 a, Vec3 b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}
inline Vec3 v3Cross(Vec3 a, Vec3 b)
{
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline f32 v3Dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
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





// Create an identity matrix
inline Mat4 mat4Identity() 
{
    Mat4 result = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    return result;
}

// Create a zero matrix
inline Mat4 mat4Zero() 
{
    Mat4 result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = 0.0f;
        }
    }
    return result;
}

// Create a translation matrix
inline Mat4 mat4Translate(Vec3 position) 
{
   	//translate matrix
	glm::mat4 translate = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, position.z));
   	
	Mat4 result;
	
	//convert glm mat4 to our mat4
	glmMat4ToMat4(translate, &result);
	 
    return result;
}


// Create a scale matrix
inline Mat4 mat4Scale(float scale) 
{
    Mat4 result = mat4Zero();
    result.m[0][0] = scale;
    result.m[1][1] = scale;
    result.m[2][2] = scale;
    result.m[3][3] = 1.0f;
    return result;
}

// Create a scale matrix with different scales per axis
inline Mat4 mat4ScaleVec(Vec3 scale) 
{
    Mat4 result = mat4Zero();
    result.m[0][0] = scale.x;
    result.m[1][1] = scale.y;
    result.m[2][2] = scale.z;
    result.m[3][3] = 1.0f;
    return result;
}

// Create rotation matrix around X axis
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

// Create rotation matrix around Y axis
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

// Create rotation matrix around Z axis
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

// Create rotation matrix for arbitrary axis
inline Mat4 mat4Rotate(float angleRadians, Vec3 axis) 
{
    Mat4 result = mat4Identity();
    
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    float t = 1.0f - c;
    
    // Normalize axis
    float len = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len > 0.0001f) {
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

// Matrix multiplication
inline Mat4 mat4Mul(Mat4 a, Mat4 b)
{

    Mat4 result = mat4Zero();

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int i = 0; i < 4; ++i) {
                result.m[col][row] += a.m[i][row] * b.m[col][i];
            }
        }
    }

    return result;
}



// Matrix addition
inline Mat4 mat4Add(Mat4 a, Mat4 b) {
    Mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = a.m[i][j] + b.m[i][j];
        }
    }
    
    return result;
}

// Matrix subtraction
inline Mat4 mat4Sub(Mat4 a, Mat4 b) {
    Mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = a.m[i][j] - b.m[i][j];
        }
    }
    
    return result;
}

// Scale all elements of a matrix
inline Mat4 mat4Scale(Mat4 a, float scale) {
    Mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = a.m[i][j] * scale;
        }
    }
    
    return result;
}

// Transform a vector by a matrix
inline Vec4 mat4TransformVec4(Mat4 m, Vec4 v)
 {
    Vec4 result;
    
    result.x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w;
    result.y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w;
    result.z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w;
    result.w = m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w;
    
    return result;
}

// Transform a 3D point (implicitly setting w=1)
inline Vec3 mat4TransformPoint(Mat4 m, Vec3 p) {
    Vec4 temp;
    
    temp.x = m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3];
    temp.y = m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3];
    temp.z = m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3];
    temp.w = m.m[3][0] * p.x + m.m[3][1] * p.y + m.m[3][2] * p.z + m.m[3][3];
    
    if (fabs(temp.w) > 0.0001f) {
        return {temp.x / temp.w, temp.y / temp.w, temp.z / temp.w};
    }
    
    return {temp.x, temp.y, temp.z};
}

// Transform a 3D direction vector (ignoring translation, implicitly setting w=0)
inline Vec3 mat4TransformDirection(Mat4 m, Vec3 d) {
    Vec3 result;
    
    result.x = m.m[0][0] * d.x + m.m[0][1] * d.y + m.m[0][2] * d.z;
    result.y = m.m[1][0] * d.x + m.m[1][1] * d.y + m.m[1][2] * d.z;
    result.z = m.m[2][0] * d.x + m.m[2][1] * d.y + m.m[2][2] * d.z;
    
    return result;
}

// Calculate the determinant of a 4x4 matrix
inline float mat4Determinant(Mat4 m) {
    // Calculate the cofactors of the first row
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
    
    // Calculate determinant using cofactors of first row
    return m.m[0][0] * c00 - m.m[0][1] * c01 + m.m[0][2] * c02 - m.m[0][3] * c03;
}

// Invert a 4x4 matrix
inline Mat4 mat4Inverse(Mat4 m) {
    float det = mat4Determinant(m);
    
    // If determinant is zero, return identity
    if (fabs(det) < 0.0001f) {
        return mat4Identity();
    }
    
    float invDet = 1.0f / det;
    Mat4 result;
    
    // Calculate the adjugate matrix (transpose of cofactor matrix)
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

// Calculate the transpose of a matrix
inline Mat4 mat4Transpose(Mat4 m) 
{
    Mat4 result;
    
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = m.m[j][i];
        }
    }
    
    return result;
}


// Copy matrix to f32** format (for compatibility with existing code)
inline void mat4ToPointers(Mat4 mat, f32** dest) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            dest[i][j] = mat.m[i][j];
        }
    }
}

// Convert f32** to Mat4 format
inline Mat4 pointersToMat4(f32** src) {
    Mat4 result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = src[i][j];
        }
    }
    return result;
}


inline Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up) 
{
	glm::mat4 lookAt = glm::lookAt(glm::vec3(eye.x, eye.y, eye.z), glm::vec3(target.x, target.y, target.z), glm::vec3(up.x, up.y, up.z));
	
	//convert glm mat4 to our mat4
	Mat4 result = {0};
	glmMat4ToMat4(lookAt, &result);
    return result;
}
inline Mat4 mat4Perspective(float fovRadians, float aspect, float nearZ, float farZ) 
{
	glm::mat4 perspective = glm::perspective(fovRadians, aspect, nearZ, farZ);
	
	Mat4 result = {0};
	
	//convert glm mat4 to our mat4
	glmMat4ToMat4(perspective, &result);
    
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
