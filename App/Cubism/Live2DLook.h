#pragma once
/*
* Live2DActorLook
* Actor Pass（モデルごとのライティング）のパラメータ
*
* 注意：Cubism SDK のヘッダーは include しないこと（Live2D.h から使うため）
*/

struct Live2DActorLook
{
	float tint[3] = { 1.0f, 1.0f, 1.0f };       // 色の乗算（明るさ・暖かさ・見た目を揃える補正）
	float saturation = 1.0f;                    // 彩度（0 で白黒）
	float opacity = 1.0f;                       // 不透明度（離席時の半透明など）

	float lightDirection[2] = { -0.6f, 0.8f };  // 光の来る方向（画面、Y 上向き。正規化しておく）
	float lightStrength = 0.0f;                 // 擬似ライトの強さ（0〜1）

	float rimColor[3] = { 1.0f, 1.0f, 1.0f };   // リムライトの色
	float rimStrength = 0.0f;                   // リムライトの強さ（0 で無効）
	float rimWidth = 3.0f;                      // リムライトの幅（ピクセル）
};
