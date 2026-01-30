#version 450 core

// These defines should match the Compute.cpp source
#define MAX_SPHERES 200
#define MAX_LIGHTS 5
#define SPHERE_ID 0
#define PLANE_ID 1
#define EPSILON 0.001
#define CHECKER_SQUARE_SIZE 0.05
#define MAX_RAY_BOUNCES 5
#define BACKGROUND_COLOR vec3(0.25, 0.05, 0.45)

layout (binding = 0, rgba32f) uniform image2D uFramebuffer;

struct Light {
	vec4 position;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
};

struct Material {
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
	float shininess;
	float reflective;
};

// FIXED: Sphere struct now matches C++ Sphere.hpp layout exactly
struct Sphere {
	vec4 center;
	vec4 ambient;
	vec4 diffuse;
	vec4 specular;
	float radius;
	float radius2;
	float shininess;
	float reflectivity;
};

struct Plane {
	Material material;
	vec3 point;
	vec3 normal;
};

// https://github.com/LWJGL/lwjgl3-wiki/wiki/2.6.1.-Ray-tracing-with-OpenGL-Compute-Shaders-%28Part-I%29
struct Camera {
	vec3 eye;
	float far;
	vec3 ray00;
	vec3 ray01;
	vec3 ray10;
	vec3 ray11;
};

struct Ray {
	vec3 origin;
	vec3 direction;
};

// the uniforms
uniform float uTime = 1.0f;
uniform Camera uCamera;
uniform Plane uPlane;
uniform Sphere uSpheres[MAX_SPHERES];
uniform Light uLights[MAX_LIGHTS];

// this is an SSBO - CRITICAL: Now uses bSpheres for all sphere data
layout (std430, binding = 1) buffer SphereBuffer {
	Sphere bSpheres[MAX_SPHERES];
};

bool sphereIntersect(in Sphere sphere, in Ray theRay, inout float t0, inout float t1)
{
	vec3 dir = theRay.direction;
	vec3 diff = theRay.origin - sphere.center.xyz;

	// quadratic formula
	//float a = dot(dir, dir);
	float b = 2.0 * dot(dir, diff);
	float c = dot(diff, diff) - sphere.radius2;

	float discriminant = (b * b) - (4.0 * c);

	if (discriminant < 0)
		return false;
	else
	{
		if (discriminant == 0)
			t0 = (-1 * b - sqrt(discriminant)) / 2.0;
		else
			t0 = min((-1 * b - sqrt(discriminant)) / 2.0, (-1 * b + sqrt(discriminant)) / 2.0);
		return true;
	}
}

// t = [norm dot ( point - ray.dir )] / [norm dot ray.dir]
bool planeIntersect(in Plane plane, in Ray theRay, inout float t0, inout float t1)
{
	float a = dot(plane.normal, theRay.direction);
	if (a == 0)
	{
		// ray is parallel to plane
		return false;
	}
	else
	{
		float b = dot(plane.normal, plane.point - theRay.origin);
		float tHit = b / a;

		t0 = tHit;
		return true;
	}
}

Material checkerboardPlaneMaterial(in vec3 intersectPoint)
{
	const int square = int(floor(intersectPoint.x * CHECKER_SQUARE_SIZE) + floor(intersectPoint.z * CHECKER_SQUARE_SIZE));

	Material planeMaterial = uPlane.material;

	if(square % 2 == 0) {
		return planeMaterial;
	}
	else {
		// black square
		planeMaterial.ambient = vec3(0.01f, 0.01f, 0.01f);
		planeMaterial.diffuse = vec3(0.01f, 0.01f, 0.01f);
		planeMaterial.specular = vec3(0.01f, 0.01f, 0.01f);
		return planeMaterial;
	}
}

/**
*   Calculate the rendering equation using Phong shading algorithm.
*   color = ambient + (shadow * (diffuse + specular));
*/
vec3 phongShading(Light light, Material material, vec3 viewDir, vec3 lightDir, vec3 intNormal, vec3 reflectDir, float shadow)
{
	vec3 diffuse, specular, ambient;
	ambient = light.ambient * 0.01f;//material.ambient;
	diffuse = light.diffuse * material.diffuse * max(dot(lightDir, intNormal), 0);
	specular = light.specular * material.specular * max(pow(dot(viewDir, reflectDir), material.shininess), 0);
	return (shadow * (diffuse + specular)) + ambient;
}

float findObjectIntersection(Ray theRay, inout int intersectObjectID, inout int objArrayIndex, float farPlane, bool endEarly)
{
	float t0, t1, tClosest = farPlane;

	for (int i = 0; i != MAX_SPHERES; ++i)
	{
		t0 = t1 = uCamera.far;
		if (sphereIntersect(bSpheres[i], theRay, t0, t1) && (t0 > EPSILON))
		{
			if (endEarly)
			{
				intersectObjectID = SPHERE_ID;
				objArrayIndex = i;
				return tClosest;
			}
			else if (!endEarly && t0 < tClosest)
			{
				intersectObjectID = SPHERE_ID;
				objArrayIndex = i;
				tClosest = t0;
			}
		}
	}

	t0 = t1 = tClosest;
	if (planeIntersect(uPlane, theRay, t0, t1) && (t0 > EPSILON) && (t0 < tClosest))
	{
		tClosest = t0;
		intersectObjectID = PLANE_ID;
		if (endEarly)
			return tClosest;
	}

	return tClosest;
}

float rand(vec2 co)
{
	return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 47236.4343);
}

// the bread and butter of the raytracer, compute a pixel color for this ray
vec3 traceRay(inout Ray theRay)
{
	vec3 finalColor = vec3(0.0f);
	float colorFrac = 0.999f;

	for (int i = 0; i != MAX_RAY_BOUNCES; ++i)
	{
		// find the closest ray-object intersection
		bool endEarly = false;
		int objArrayIndex = -1;
		int intersectObjectID = -1;
		float tClosest = findObjectIntersection(theRay, intersectObjectID, objArrayIndex, uCamera.far, endEarly);

		if (intersectObjectID == -1)
		{
			// No intersection - render gradient background
			vec2 coords = vec2(gl_GlobalInvocationID.xy);
			vec2 size = vec2(imageSize(uFramebuffer));

			float r = float(coords.x) / float(size.x);
			float g = float(coords.y) / float(size.y);
			float b = fract(uTime);
			vec3 bg = vec3(r, g, b);
			finalColor += bg;
			break;
		}

		// Find the point of intersection with the object
		vec3 intPoint = vec3(theRay.origin + (theRay.direction * tClosest));
		vec3 intNormal;
		float reflValue;

		Material activeMaterial;
		if (intersectObjectID == SPHERE_ID)
		{
			// Create material from sphere's direct fields (no nested Material struct)
			activeMaterial = Material(bSpheres[objArrayIndex].ambient.xyz,
								bSpheres[objArrayIndex].diffuse.xyz,
								bSpheres[objArrayIndex].specular.xyz,
								bSpheres[objArrayIndex].shininess,
								bSpheres[objArrayIndex].reflectivity);
			intNormal = normalize(vec3(intPoint - bSpheres[objArrayIndex].center.xyz));
			reflValue = bSpheres[objArrayIndex].reflectivity;
		}
		else if (intersectObjectID == PLANE_ID)
		{
			intNormal = uPlane.normal;
			reflValue = uPlane.material.reflective;
			activeMaterial = checkerboardPlaneMaterial(intPoint);
		}

		// make sure we didn't intersect from inside the active obj
		if (dot(theRay.direction, intNormal) > 0.0)
			intNormal = -1.0 * intNormal;

		vec3 localColor = vec3(0.0);

		// now iterate through the lights and look for shadows
		for (int i = 0; i != MAX_LIGHTS; ++i)
		{
			float shadow = 1.0f;
			Light activeLight = uLights[i];

			vec3 lightDir;
			// FIXED: w=0 means directional, w=1 means point light
			if (activeLight.position.w == 0.0)
				lightDir = normalize(vec3(activeLight.position.xyz));
			else
				lightDir = normalize(vec3(activeLight.position.xyz - intPoint));

			Ray lightRay = Ray(intPoint + (intNormal * EPSILON), lightDir);

			// Shadow ray testing
			endEarly = true;
			intersectObjectID = -1;
			objArrayIndex = -1;
			tClosest = findObjectIntersection(lightRay, intersectObjectID, objArrayIndex, length(lightDir), endEarly);

			// FIXED: Removed duplicate sphereIntersect check
			if (intersectObjectID != -1)
			{
				shadow = 0.100f;
			}

			// compute lighting
			vec3 reflectDir = reflect(lightRay.direction, intNormal);

			localColor += phongShading(activeLight, activeMaterial, theRay.direction, lightRay.direction, intNormal, reflectDir, shadow);
		} // end lights

		finalColor += localColor * (1.0f - reflValue) * colorFrac;

		colorFrac *= reflValue;

		if (reflValue > 0.0)
		{
			vec3 reflectDir = normalize(reflect(theRay.direction, intNormal));
			Ray reflectRay = Ray(intPoint + (intNormal * EPSILON), reflectDir);
			theRay = reflectRay;
		}

		// More aggressive early termination for performance
		if (colorFrac < 0.01f)
			break;

	} // end for loop ray bounces

	return finalColor;
} // end traceRay

layout (local_size_x = 20, local_size_y = 20) in;
void main()
{
	// calculate the viewing frustum (camera)
	ivec2 invocID = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(uFramebuffer);

	if (invocID.x >= size.x || invocID.y >= size.y)
		return;

	vec2 pixelPos = vec2(invocID) / vec2(size.x - 1, size.y - 1);

	vec3 cameraDir = mix(mix(uCamera.ray00, uCamera.ray01, pixelPos.y), mix(uCamera.ray10, uCamera.ray11, pixelPos.y), pixelPos.x);

	Ray theRay = Ray(uCamera.eye, normalize(cameraDir));

	vec3 finalColor = traceRay(theRay);

	imageStore(uFramebuffer, invocID, vec4(finalColor, 1.0));
}

