// Keep bilinear border samples premultiplied to avoid dark transparent fringes.
#define USE_PM_ALPHA 1

uniform float center_x_percent<
    string label = "Center x percent";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 100.0;
    float step = 0.01;
> = 50.0;
uniform float center_y_percent<
    string label = "Center y percent";
    string widget_type = "slider";
    float minimum = 0.0;
    float maximum = 100.0;
    float step = 0.01;
> = 50.0;
uniform float power_x<
    string label = "Power x";
    string widget_type = "slider";
    float minimum = -2.0;
    float maximum = 2.0;
    float step = 0.01;
> = 1.75;
uniform float power_y<
    string label = "Power y";
    string widget_type = "slider";
    float minimum = -2.0;
    float maximum = 2.0;
    float step = 0.01;
> = 1.75;

float4 mainImage(VertData v_in) : TARGET
{
    float2 center_pos = float2(center_x_percent * .01, center_y_percent * .01);
    float2 uv = v_in.uv;
    // normalize(0) is undefined; the optical center maps to itself.
    if (distance(center_pos, v_in.uv) == 0.0)
        return image.Sample(textureSampler, uv);
    if (power_x >= 0.0001){
        float b = sqrt(dot(center_pos, center_pos));
        uv.x = (center_pos  + normalize(v_in.uv - center_pos) * tan(distance(center_pos, v_in.uv) * power_x) * b / tan( b * power_x)).x;
    } else if(power_x <= -0.0001){
        float b;
        if (uv_pixel_interval.x < uv_pixel_interval.y){
            b = center_pos.x;
        } else {
            b = center_pos.y;
        }
        uv.x = (center_pos  + normalize(v_in.uv - center_pos) * atan(distance(center_pos, v_in.uv) * -power_x * 10.0) * b / atan(-power_x * b * 10.0)).x;
    }
    if (power_y >= 0.0001){
        float b = sqrt(dot(center_pos, center_pos));
        uv.y = (center_pos  + normalize(v_in.uv - center_pos) * tan(distance(center_pos, v_in.uv) * power_y) * b / tan( b * power_y)).y;
    } else if(power_y <= -0.0001){
        float b;
        if (uv_pixel_interval.x < uv_pixel_interval.y){
            b = center_pos.x;
        } else {
            b = center_pos.y;
        }
        uv.y = (center_pos  + normalize(v_in.uv - center_pos) * atan(distance(center_pos, v_in.uv) * -power_y * 10.0) * b / atan(-power_y * b * 10.0)).y;
    }
    return image.Sample(textureSampler, uv);
}
