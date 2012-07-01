uniform mat4 modelviewMatrix;
uniform mat4 modelviewprojectionMatrix;
uniform mat3 normalMatrix;
attribute vec4 in_position;
attribute vec3 in_normal;
attribute vec4 in_color;
vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);

varying vec4 vVaryingColor;

void main()
{
	gl_Position = modelviewprojectionMatrix * in_position;
	vec3 vEyeNormal = normalMatrix * in_normal;
	vec4 vPosition4 = modelviewMatrix * in_position;
	vec3 vPosition3 = vPosition4.xyz / vPosition4.w;
	vec3 vLightDir = normalize(lightSource.xyz - vPosition3) + vec3(2.0, 3.0, 4.0);
	float diff = max(0.0, dot(vEyeNormal, vLightDir));
	vVaryingColor = vec4(diff * in_color.rgb, 1.0);
}

