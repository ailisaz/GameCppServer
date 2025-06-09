#pragma once

#include <string>

// 使用 `nlohmann/json` 库进行JSON序列化
#include <nlohmann/json.hpp>

// 明确命名空间，避免污染全局
namespace Game {

// 食物的数据结构，与Java版本保持一致
struct FoodData {
    int id;
    float x, y;
    static constexpr float RADIUS = 20.0f;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(FoodData, id, x, y); // nlohmann/json 自动序列化/反序列化
};

// 玩家的数据结构，与Java版本保持一致
struct PlayerData {
    int id;
    std::string name;
    float x, y;
    int score;
    std::string colorHex;

    PlayerData() = default; // 默认构造函数

    PlayerData(int id, std::string name, float x, float y, std::string colorHex)
        : id(id), name(std::move(name)), x(x), y(y), score(0), colorHex(std::move(colorHex)) {}

    // 使用 nlohmann/json 的宏来轻松实现JSON转换
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlayerData, id, name, x, y, score, colorHex);
};

} // namespace Game