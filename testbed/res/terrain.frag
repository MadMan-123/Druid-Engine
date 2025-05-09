#version 450 core


// UBO for lighting
uniform vec3 lightPos;  // Add these
uniform vec3 lightColor;
uniform float lightIntensity;
uniform vec3 ambientColor;


uniform sampler2D grassTexture;
uniform sampler2D stoneTexture;
uniform sampler2D snowTexture;
in float Height;


const float uNoiseHeight = 50;
float textureScale = 0.5;
in vec2 TexCoord;
in vec3 WorldPos;
in vec3 WorldNormal;

out vec4 FragColor;

vec3 calculateLighting(vec3 normal, vec3 worldPos) {
    vec3 lightDir = normalize(lightPos - worldPos);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 diffuse = diff * lightColor;
    vec3 ambient = ambientColor;

    // Final should be white if lightColor is white
    return ambient + diffuse;
}


void main()
{

    vec3 texColor;
    vec2 scaledTexCoord = TexCoord * textureScale;
    // Ensure normal is normalized
    vec3 normal = normalize(WorldNormal);
    vec3 lighting = calculateLighting(normal, WorldPos) ;
    //ensure the lighting is white
   
    

    
    if (Height / 25 > uNoiseHeight) 
    {
        FragColor = texture(snowTexture, scaledTexCoord) * vec4(lighting , 1.0);
    }
    else if (Height / 25 > uNoiseHeight - 8.5) 
    {
        FragColor = texture(stoneTexture, scaledTexCoord) * vec4(lighting , 1.0);
    }
    else 
    {
        FragColor = texture(grassTexture, scaledTexCoord) * vec4(lighting , 1.0);
    }


}

