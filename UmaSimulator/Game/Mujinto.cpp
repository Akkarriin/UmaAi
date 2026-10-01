#include <iostream>
#include <cassert>
#include <algorithm>
#include "Game.h"
using namespace std;

//无人岛剧本（無人島へようこそ -DESIGN YOUR ISLAND-）的规则
//数据来源：umasim 的 data/mujinto_memo.md（作者声明可转载）以及实测表，按说明重新实现，没有逐行翻译umasim的代码
//回合数：本模拟器从0开始，umasim与游戏内从1开始，t = T - 1

static bool randBool(mt19937_64& rand, double p)
{
  return rand() % 65536 < p * 65536;
}

//训练tra是否会提升属性status（普通训练基础值不为0的属性）
static bool trainingRaisesStatus(int tra, int status)
{
  static const bool table[5][5] =
  {
    {1,0,1,0,0},//速：速力
    {0,1,0,1,0},//耐：耐根
    {0,1,1,0,0},//力：耐力
    {1,0,1,1,0},//根：速力根
    {1,0,0,0,1},//智：速智
  };
  return table[tra][status];
}

MujintoFacility::MujintoFacility() :type(-1), level(0), jukuren(false)
{
}

MujintoFacility::MujintoFacility(int type, int level, bool jukuren) : type(type), level(level), jukuren(jukuren)
{
}

int MujintoFacility::space() const
{
  if (type == MJ_house)
    return level == 1 ? 2 : 3;
  if (level == 5)
    return jukuren ? 2 : 3;
  if (level >= 3)
    return jukuren ? 1 : 2;
  return 1;
}

std::string MujintoFacility::toString() const
{
  static const string names[6] = { "速","耐","力","根","智","海之家" };
  if (type < 0)return "空";
  string s = names[type] + "Lv" + to_string(level);
  if (type != MJ_house && level >= 3)
    s += jukuren ? "(熟练)" : "(本能)";
  return s;
}

//单个设施给岛训练/岛合宿的属性加成，status=0~5速耐力根智pt
static int facilityIslandBonus(int type, int level, bool jukuren, int status)
{
  if (level <= 0)return 0;
  if (type == MJ_house)
    return status == 5 ? 1 : 0;
  if (type == status)
  {
    if (level == 5)return jukuren ? 3 : 7;
    if (level == 4)return jukuren ? 2 : 5;
    if (level == 3)return jukuren ? 2 : 3;
    if (level == 2)return 2;
    return 1;
  }
  if (status == 5)
  {
    if (jukuren)return level >= 4 ? 1 : 0;
    if (level == 5)return 2;
    if (level >= 3)return 1;
    return 0;
  }
  //Lv5：这个训练的副属性+1（根性训练的速度不加）
  if (level == 5 && trainingRaisesStatus(type, status) && !(type == MJ_guts && status == MJ_speed))
    return 1;
  return 0;
}

//单个设施在岛合宿时给对应训练的训练效果%
static int facilityCampTrainingEffect(int type, int level, bool jukuren)
{
  if (level <= 0 || type == MJ_house)return 0;
  if (type == MJ_speed)
  {
    if (level == 5)return jukuren ? 25 : 50;
    if (level == 4)return jukuren ? 15 : 30;
    if (level == 3)return jukuren ? 10 : 20;
    if (level == 2)return 10;
    return 5;
  }
  if (level == 5)return jukuren ? 30 : 60;
  if (level == 4)return jukuren ? 25 : 40;
  if (level == 3)return jukuren ? 20 : 30;
  if (level == 2)return 20;
  return 10;
}

void Game::mj_init()
{
  if (friend_type == 0)
    mj_linkMode = 0;
  else
    mj_linkMode = friend_level >= 41 ? 2 : 1;
  for (int i = 0; i < 6; i++)
  {
    mj_facilityLevel[i] = 0;
    mj_facilityJukuren[i] = false;
    mj_plan[i] = MujintoFacility();
    mj_evalResult[i] = 0;
    mj_deyilvBonus[i] = 0;
    mj_deyilvBonusNext[i] = 0;
  }
  mj_planNum = 0;
  mj_pioneerPt = 0;
  mj_requiredPt1 = 0;
  mj_requiredPt2 = 0;
  mj_ticket = 0;
  mj_bonusTrainingEffect = 0;
  mj_bonusHint = 0;
  mj_bonusPioneerPt = 0;
  mj_guestNum = 0;
  for (int i = 0; i < MJ_MAX_GUEST; i++)
    mj_guestType[i] = 0;
  for (int i = 0; i < 5; i++)
    mj_trainPioneerPt[i] = 0;
}

int Game::mj_facilityIslandBonus(int status) const
{
  int total = 0;
  for (int f = 0; f < 6; f++)
    total += facilityIslandBonus(f, mj_facilityLevel[f], mj_facilityJukuren[f], status);
  return total;
}

int Game::mj_campTrainingEffect(int tra) const
{
  //只有同类型的设施生效（海之家的训练效果是0）
  return facilityCampTrainingEffect(tra, mj_facilityLevel[tra], mj_facilityJukuren[tra]);
}

int Game::mj_specialtyRateUp(int cardType) const
{
  if (cardType < 0 || cardType >= 5)return 0;
  int level = mj_facilityLevel[cardType];
  bool jukuren = mj_facilityJukuren[cardType];
  int normal = 0;
  if (!jukuren)
    normal = level == 5 ? 80 : level == 4 ? 50 : level == 3 ? 20 : 0;
  if (!isCampTraining())
    return normal;
  int camp = 0;
  if (level == 5)camp = jukuren ? 50 : 100;
  else if (level == 4)camp = jukuren ? 30 : 50;
  else if (level == 3)camp = jukuren ? 20 : 60;
  return normal + camp;
}

int Game::mj_positionRateUp() const
{
  int level = mj_facilityLevel[MJ_house];
  if (level == 0)return 0;
  return level >= 3 ? 20 : 10;
}

int Game::mj_hintRateUp(int tra) const
{
  int total = mj_bonusHint;
  int houseLevel = mj_facilityLevel[MJ_house];
  total += houseLevel == 3 ? 200 : houseLevel == 2 ? 100 : 0;
  if (isCampTraining() && mj_facilityJukuren[tra] && mj_facilityLevel[tra] == 3)
    total += 200;
  return total;
}

bool Game::mj_alwaysHint(int tra) const
{
  return isCampTraining() && mj_facilityJukuren[tra] && mj_facilityLevel[tra] >= 4;
}

int Game::mj_trainingEffectByFriend() const
{
  return mj_facilityLevel[MJ_house] > 0 ? 5 : 0;
}

bool Game::mj_canGainPioneerPt() const
{
  return turn >= 2 && turn < 60;
}

int Game::mj_calcTrainingPioneerPt(int headNum, bool shining) const
{
  if (!mj_canGainPioneerPt())return 0;
  int bonus = mj_bonusPioneerPt + (shining ? 20 : 0);
  return (60 + headNum * 6) * (100 + bonus) / 100;
}

int Game::mj_calcRacePioneerPt(bool isGoalRace) const
{
  if (!mj_canGainPioneerPt())return 0;
  int bonus = mj_bonusPioneerPt;
  if (isGoalRace || mj_facilityLevel[MJ_house] >= 3)
    bonus += 200;
  return 20 * (100 + bonus) / 100;
}

void Game::mj_addPioneerPt(int pt)
{
  int oldPt = mj_pioneerPt;
  mj_pioneerPt += pt;
  if (isCampTraining())//合宿期间不建设，合宿结束后补建
    return;
  if (oldPt < mj_requiredPt1 && mj_pioneerPt >= mj_requiredPt1)
    mj_buildPlan(false);
  if (oldPt < mj_requiredPt2 && mj_pioneerPt >= mj_requiredPt2)
    mj_buildPlan(true);
}

void Game::mj_buildPlan(bool all)
{
  int totalCost = 0;
  int built = 0;
  while (built < mj_planNum && (all || totalCost < mj_requiredPt1))
  {
    const MujintoFacility& f = mj_plan[built];
    mj_facilityLevel[f.type] = f.level;
    mj_facilityJukuren[f.type] = f.jukuren;
    totalCost += f.space() * 100;
    built++;
    printEvents("设施建成：" + f.toString());
  }
  for (int i = built; i < mj_planNum; i++)
    mj_plan[i - built] = mj_plan[i];
  mj_planNum -= built;

  //岛训练券，资深年后半以外最多持有1张
  if (turn >= 60)
    mj_ticket += 1;
  else
    mj_ticket = 1;
}

void Game::mj_upgradeAfterCamp()
{
  if (mj_pioneerPt >= mj_requiredPt2)
    mj_buildPlan(true);
  else if (mj_pioneerPt >= mj_requiredPt1)
    mj_buildPlan(false);
}

void Game::mj_evaluationAndPlan(std::mt19937_64& rand, int phase)
{
  assert(phase >= 0 && phase <= 5);
  //评价会（第0次只有制定计划，没有评价）
  if (phase >= 1)
  {
    bool great = mj_pioneerPt >= mj_requiredPt2;
    int g = great ? 1 : 0;
    mj_evalResult[phase] = great ? 2 : 1;
    addAllStatus(GameConstants::MJ_EvalStatus[g][phase]);
    skillPt += GameConstants::MJ_EvalPt[g][phase];
    if (great)
    {
      int hp = (mj_pioneerPt - mj_requiredPt2) / GameConstants::MJ_EvalHpDivider[phase];
      addVital(std::min(hp, GameConstants::MJ_EvalHpMax[phase]));
      addJiBan(PS_noncardYayoi, 5, 2);
    }
    addRandomCardHint(rand);//随机一张支援卡的技能hint
    if (phase == 5)//全身全霊Lv1或者末脚Lv2
      skillPt += int((great ? 1 : 2) * gameSettings.hintPtRate);
    mj_bonusTrainingEffect = GameConstants::MJ_EvalBonus[g][phase][0];
    mj_bonusHint = GameConstants::MJ_EvalBonus[g][phase][1];
    mj_bonusPioneerPt = GameConstants::MJ_EvalBonus[g][phase][2];
    printEvents(string("第") + to_string(phase) + "次评价会：" + (great ? "大好评" : "好评"));
  }
  if (phase == 2 || phase == 4)//全训练等级+1
  {
    for (int i = 0; i < 5; i++)
      addTrainingLevelCount(i, 4);
  }
  if (GameConstants::MJ_GuestTotal[phase] > 0)
    mj_addGuests(rand, GameConstants::MJ_GuestTotal[phase]);

  //下一期的建设计划
  int space = GameConstants::MJ_FacilitySpace[mj_linkMode][phase];
  mj_pioneerPt = 0;
  mj_requiredPt1 = space / 2 * 100;
  mj_requiredPt2 = space * 100;
  mj_planNum = 0;
  if (phase < 5)
    mj_makeDefaultPlan(phase);
}

void Game::mj_makeDefaultPlan(int phase)
{
  //手写的默认计划：海之家Lv1最优先，然后按卡组类型（速度额外优先）升级，同类设施倾向于继续升级，本能优先
  //每种设施在一次计划里最多出现一次，按优先度贪心填满格数
  int space = GameConstants::MJ_FacilitySpace[mj_linkMode][phase];
  double weight[6] = { 2.5,1,1,1,1,0 };
  for (int i = 0; i < 6; i++)
  {
    if (persons[i].personType == PersonType_card)
      weight[persons[i].cardParam.cardType] += 1;
  }
  bool used[6] = { false,false,false,false,false,false };
  int rest = space;
  mj_planNum = 0;
  while (true)
  {
    int best = -1;
    double bestScore = -1;
    MujintoFacility bestFacility;
    for (int t = 0; t < 6; t++)
    {
      if (used[t])continue;
      int cur = mj_facilityLevel[t];
      int maxLevel = t == MJ_house ? 3 : 5;
      if (cur >= maxLevel)continue;
      MujintoFacility f(t, cur + 1, cur >= 3 ? mj_facilityJukuren[t] : false);
      if (f.space() > rest)continue;
      double score = t == MJ_house ? (cur == 0 ? 1000 : 0.5) : weight[t] * (1 + 0.2 * cur);
      if (score > bestScore)
      {
        bestScore = score;
        best = t;
        bestFacility = f;
      }
    }
    if (best < 0)break;
    used[best] = true;
    mj_plan[mj_planNum++] = bestFacility;
    rest -= bestFacility.space();
  }
}

void Game::mj_addGuests(std::mt19937_64& rand, int totalCount)
{
  int ownCards = 0;
  for (int i = 0; i < 6; i++)
    if (persons[i].personType == PersonType_card)
      ownCards++;
  int target = std::min(MJ_MAX_GUEST, totalCount - ownCards);
  while (mj_guestNum < target)
  {
    mj_guestType[mj_guestNum] = rand() % 5;
    mj_guestNum++;
  }
}

void Game::mj_addDeyilvNextTurnAll(int value)
{
  for (int i = 0; i < 6; i++)
    if (persons[i].personType == PersonType_card)
      mj_deyilvBonusNext[i] += value;
}

//塔克布莱恩-----------------------------------------------------------------------------------

void Game::handleFriendClickEvent(std::mt19937_64& rand, int atTrain)
{
  assert(friend_type != 0 && (friend_personId < 6 && friend_personId >= 0) && persons[friend_personId].personType == PersonType_scenarioCard);
  if (friend_stage == FriendStage_notClicked)
  {
    printEvents("第一次点友人");
    friend_stage = FriendStage_beforeUnlockOutgoing;
    addVitalMax(4);
    addMotivation(1);
    addStatusFriend(0, 5);
    addStatusFriend(1, 5);
    addStatusFriend(2, 5);
    addJiBan(friend_personId, 10, 1);
  }
  else
  {
    //训练后事件，概率待测
    if (!randBool(rand, GameConstants::FriendClickEventProb))
      return;
    addJiBan(friend_personId, 5, 1);
    addStatusFriend(1, 5);
    mj_addDeyilvNextTurnAll(20);
    if (randBool(rand, GameConstants::FriendClickEventGreatProb))
      addMotivation(1);
  }
}

void Game::handleFriendUnlock(std::mt19937_64& rand)
{
  assert(friend_stage == FriendStage_beforeUnlockOutgoing);
  addJiBan(friend_personId, 5, 1);
  addMotivation(1);
  //上：体力+20，pt+10；下：速耐根+6。体力缺得多就选上
  if (maxVital - vital >= 20)
  {
    addVitalFriend(20);
    addStatusFriend(5, 10);
  }
  else
  {
    addStatusFriend(0, 6);
    addStatusFriend(1, 6);
    addStatusFriend(3, 6);
  }
  friend_stage = FriendStage_afterUnlockOutgoing;
  printEvents("友人外出解锁！");
}

void Game::runFriendOutgoing(std::mt19937_64& rand, bool chooseUpper)
{
  assert(friend_type != 0 && friend_stage == FriendStage_afterUnlockOutgoing && friend_outgoingNum < 5);
  int pid = friend_personId;
  int idx = friend_outgoingNum;
  addJiBan(pid, 5, 1);
  addMotivation(1);
  if (idx == 0)
  {
    addVitalMax(4);
    addVitalFriend(20);
    addStatusFriend(1, 35);
    mj_addDeyilvNextTurnAll(20);
  }
  else if (idx == 1)
  {
    addVitalFriend(20);
    addStatusFriend(0, 30);
    skillPt += int(2 * gameSettings.hintPtRate);//活路を拓く！Lv2
    mj_addDeyilvNextTurnAll(20);
  }
  else if (idx == 2)
  {
    if (chooseUpper)
      addVitalFriend(50);
    else
    {
      for (int i = 0; i < 5; i++)
        addStatusFriend(i, 8);
      addStatusFriend(5, 10);
    }
    mj_addDeyilvNextTurnAll(30);
  }
  else if (idx == 3)
  {
    addVitalFriend(25);
    addStatusFriend(1, 15);
    addStatusFriend(3, 15);
    mj_addDeyilvNextTurnAll(20);
  }
  else if (idx == 4)
  {
    addVitalFriend(30);
    addStatusFriend(2, 30);
    skillPt += int(3 * gameSettings.hintPtRate);//パスファインダーLv3
    mj_addDeyilvNextTurnAll(120);
  }
  friend_outgoingNum++;

  //出行后的选择：上是pt+3和发展pt（Lv20为30，Lv50为80，中间按线性估计），下只有pt+3，总是选上
  skillPt += 3;
  if (mj_canGainPioneerPt())
    mj_addPioneerPt(30 + (friend_level - 20) * 50 / 30);
}

void Game::handleFriendFixedEvent()
{
  if (friend_type == 0)return;//没友人卡
  if (friend_stage < FriendStage_beforeUnlockOutgoing)return;//没点过就没事件
  if (turn == 23)//经典年新年
  {
    addVitalFriend(15);
    addMotivation(1);
    addStatusFriend(0, 10);
    addJiBan(friend_personId, 5, 1);
    skillPt += int(3 * gameSettings.hintPtRate);//端緒Lv3
  }
  else if (turn == TOTAL_TURN - 1)
  {
    int v = friend_outgoingNum >= 5 ? 18 : 10;
    addStatusFriend(0, v);
    addStatusFriend(1, v);
    addStatusFriend(3, v);
    addStatusFriend(5, friend_outgoingNum >= 5 ? 65 : 40);
  }
}
