#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../config.h"

const int TOTAL_TURN = 78;
const int MAX_INFO_PERSON_NUM = 6;//有单独信息的人头个数（支援卡），无人岛的嘉宾不单独存Person
const int MJ_MAX_GUEST = 11;//无人岛剧本最多11个嘉宾（PJ参加者16人，包括自己的非友人卡）

class GameConstants
{
public:
  static const int TrainingBasicValue[5][5][7]; //TrainingBasicValue[颜色][第几种训练][LV几][速耐力根智pt体力]
  static const int FailRateBasic[5][5];//[第几种训练][LV几]，失败率= 0.025*(x0-x)^2 + 1.25*(x0-x)
  static const int BasicFiveStatusLimit[5];//初始上限，1200以上翻倍

  //各种游戏参数
  //static const int NormalRaceFiveStatusBonus;//常规比赛属性加成=3，特殊马娘特殊处理（狄杜斯等）
  //static const int NormalRacePtBonus;//常规比赛pt加成
  static const double EventProb;//每回合有EventProb概率随机一个属性以及pt +EventStrengthDefault，模拟支援卡事件
  static const int EventStrengthDefault;

  //剧本卡相关（塔克布莱恩）
  static const int FriendCardIdSSR = 30257;//SSR [本能は吼えているか！？]タッカーブライン
  static const int FriendCardIdR = 10128;//R [無人島PJ責任者]タッカーブライン
  static const double FriendUnlockOutgoingProbEveryTurnLowFriendship;//每回合解锁外出的概率，羁绊小于60
  static const double FriendUnlockOutgoingProbEveryTurnHighFriendship;//每回合解锁外出的概率，羁绊大于等于60
  //static const double FriendEventProb;//友人事件概率//常数0.4写死在对应函数里了
  static const double FriendClickEventProb;//友人训练后事件的概率（待测）
  static const double FriendClickEventGreatProb;//友人训练后事件大成功（心情+1）的概率（待测）


  //剧本相关
  static const std::vector<int> LinkCharas;// Link角色

  //无人岛剧本，数据来自 umasim 的 mujinto_memo.md 与实测表
  static const int MJ_CampTrainingValue[2][5][7];//岛合宿训练基础值[设施是否已建][训练][速耐力根智pt体力]
  static const int MJ_FacilitySpace[3][6];//建设计划可用格数[linkMode][phase]
  static const int MJ_EvalStatus[2][6];//评价会全属性[好评/大好评][phase]
  static const int MJ_EvalPt[2][6];//评价会技能点[好评/大好评][phase]
  static const int MJ_EvalHpDivider[6];//大好评时超出的发展pt每多少换1体力
  static const int MJ_EvalHpMax[6];//大好评体力上限
  static const int MJ_EvalBonus[2][6][3];//评价会后的加成[好评/大好评][phase]{训练效果,hint率,发展pt加成}
  static const int MJ_GuestTotal[6];//评价会后PJ参加人数（含自己的非友人卡），0表示不变


  //评分
  static const int FiveStatusFinalScore[1200+800*2+1];//不同属性对应的评分
  static const double ScorePtRateDefault;//为了方便，直接视为每1pt对应多少分。
  static const double HintLevelPtRateDefault;//为了方便，直接视为每一级hint多少pt。
  static const double HintProbTimeConstantDefault;//为了方便，直接视为每一级hint多少pt。
  //static const double ScorePtRateQieZhe;//为了方便，直接视为每1pt对应多少分。切者

  static bool isLinkChara(int id);

};