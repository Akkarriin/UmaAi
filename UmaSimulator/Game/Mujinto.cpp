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
  mj_planPending = false;
  mj_planSpace = 0;
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
  for (int i = 0; i < 5; i++)
    mj_islandHouse[i] = -1;
  mj_islandHouseNum = 0;
  for (int i = 0; i < 6; i++)
  {
    mj_islandValue[i] = 0;
    mj_islandValueLower[i] = 0;
  }
  mj_islandPioneerPt = 0;
  mj_islandFriendCount = 0;
  mj_islandFriendPositions = 0;
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
  int houseHint = houseLevel == 3 ? 200 : houseLevel == 2 ? 100 : 0;
  total += houseHint;
  if (isCampTraining())
  {
    //按 umasim：岛合宿时海之家的hint率再算一次，同类熟练Lv3再+200
    total += houseHint;
    if (tra >= 0 && tra < 5 && mj_facilityJukuren[tra] && mj_facilityLevel[tra] == 3)
      total += 200;
  }
  return total;
}

bool Game::mj_alwaysHint(int tra) const
{
  return isCampTraining() && tra >= 0 && tra < 5 && mj_facilityJukuren[tra] && mj_facilityLevel[tra] >= 4;
}

bool Game::mj_campHintAll(int tra) const
{
  return isCampTraining() && tra >= 0 && tra < 5 && mj_facilityJukuren[tra] && mj_facilityLevel[tra] == 5;
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

  //岛训练券：按 umasim，建设时券数变成1（已经有券的话不叠加）
  mj_ticket = 1;
}

int Game::mj_ptToNextTicket() const
{
  if (mj_pioneerPt < mj_requiredPt1)
    return mj_requiredPt1 - mj_pioneerPt;
  if (mj_pioneerPt < mj_requiredPt2)
    return mj_requiredPt2 - mj_pioneerPt;
  return 100000;
}

bool Game::mj_pioneerPtWouldWasteTicket(int pt) const
{
  //umasim 用的余量是70：差得不到 pt+70 就当作会拿到下一张券
  return mj_ticket > 0 && mj_ptToNextTicket() < pt + 70;
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
  mj_planSpace = space;
  //第5次评价会后没有计划；其余的计划在下回合开始前的ST_plan里逐个选
  mj_planPending = phase < 5;
  if (mj_planPending && !mj_hasPlanCandidate())
    mj_planPending = false;
}

MujintoFacility Game::mj_planCandidate(int idx) const
{
  int type = idx / 2;
  bool jukuren = idx % 2 == 1;
  int level = mj_facilityLevel[type] + 1;
  return MujintoFacility(type, level, jukuren);
}

int Game::mj_planSpaceUsed() const
{
  int used = 0;
  for (int i = 0; i < mj_planNum; i++)
    used += mj_plan[i].space();
  return used;
}

bool Game::mj_isPlanCandidateLegal(int idx) const
{
  if (!mj_planPending)return false;
  if (idx < 0 || idx >= 12)return false;
  int type = idx / 2;
  bool jukuren = idx % 2 == 1;
  //每种设施在一期计划里最多一次
  for (int i = 0; i < mj_planNum; i++)
    if (mj_plan[i].type == type)return false;
  int level = mj_facilityLevel[type] + 1;
  int maxLevel = type == MJ_house ? 3 : 5;
  if (level > maxLevel)return false;
  //Lv3时选本能或熟练，Lv4、5沿用，Lv1、2和海之家只有本能
  if (type == MJ_house || level <= 2)
  {
    if (jukuren)return false;
  }
  else if (level >= 4)
  {
    if (jukuren != mj_facilityJukuren[type])return false;
  }
  return MujintoFacility(type, level, jukuren).space() <= mj_planSpace - mj_planSpaceUsed();
}

bool Game::mj_hasPlanCandidate() const
{
  for (int idx = 0; idx < 12; idx++)
    if (mj_isPlanCandidateLegal(idx))return true;
  return false;
}

void Game::mj_addPlan(int idx)
{
  assert(stage == ST_plan && mj_isPlanCandidateLegal(idx));
  mj_plan[mj_planNum++] = mj_planCandidate(idx);
  if (!mj_hasPlanCandidate())
  {
    mj_planPending = false;
    stage = ST_distribute;
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

//岛训练-------------------------------------------------------------------------------------

//岛训练的基础值，速耐力根智pt
static const int MJ_IslandBase[6] = { 8,6,4,4,10,7 };

//站在某个设施（0~4）或者海之家（5）时，支援卡的友情/干劲/训练加成反映到各属性的倍率
static const double MJ_IslandRate[6][6] =
{
  {0.6, 0,   0.4, 0,   0,   0.5},//速
  {0,   0.5, 0,   0.3, 0,   0.7},//耐
  {0,   0.3, 0.6, 0,   0,   0.7},//力
  {0.3, 0,   0.3, 0.5, 0,   0.7},//根
  {0.45,0,   0,   0,   1.0, 0.7},//智
  {0.2, 0.15,0.2, 0.15,0.15,0.7},//海之家
};

//单个设施给岛训练的训练效果%：海之家对所有属性有效，其他设施对这个训练会提升的属性（包括pt）有效
static int facilityIslandTrainingEffect(int type, int level, bool jukuren, int status)
{
  if (level <= 0)return 0;
  if (type == MJ_house)
    return level == 3 ? 15 : level == 2 ? 10 : 5;
  if (status != 5 && !trainingRaisesStatus(type, status))
    return 0;
  if (level == 5)return jukuren ? 15 : 25;
  if (level == 4)return jukuren ? 10 : 15;
  if (level == 3)return jukuren ? 5 : 10;
  if (level == 2)return 5;
  return 0;
}

int Game::mj_islandTrainingEffect(int status) const
{
  int total = 0;
  for (int f = 0; f < 6; f++)
    total += facilityIslandTrainingEffect(f, mj_facilityLevel[f], mj_facilityJukuren[f], status);
  return total;
}

bool Game::mj_isIslandTrainingAvailable() const
{
  return stage == ST_train && !isRacing && mj_ticket > 0 && !isCampTraining();
}

int Game::mj_islandMembers(int* memberId, int* memberPos) const
{
  int memberNum = 0;
  for (int t = 0; t < 5; t++)
  {
    for (int h = 0; h < 5; h++)
    {
      int p = personDistribution[t][h];
      if (p < 0)break;
      if (p == PS_noncardYayoi || p == PS_noncardReporter)continue;
      memberId[memberNum] = p;
      memberPos[memberNum] = t;
      memberNum++;
    }
  }
  for (int i = 0; i < mj_islandHouseNum; i++)
  {
    memberId[memberNum] = mj_islandHouse[i];
    memberPos[memberNum] = 5;
    memberNum++;
  }
  return memberNum;
}

void Game::mj_calculateIslandTraining(std::mt19937_64* rand)
{
  if (!mj_isIslandTrainingAvailable() && rand != nullptr)
  {
    mj_islandHouseNum = 0;
    for (int i = 0; i < 5; i++)
      mj_islandHouse[i] = -1;
    for (int i = 0; i < 6; i++)
      mj_islandValue[i] = mj_islandValueLower[i] = 0;
    mj_islandPioneerPt = 0;
    return;
  }

  //每个人头的位置：0~4设施，5海之家（按 umasim：站位的人留在原地，不管设施建没建）
  //没站位的人去海之家，最多5人，友人>支援卡>嘉宾，同类随机（不管海之家建没建）
  if (rand != nullptr)
  {
    bool placed[MAX_INFO_PERSON_NUM] = { false,false,false,false,false,false };
    bool guestPlaced[MJ_MAX_GUEST] = {};
    for (int t = 0; t < 5; t++)
    {
      for (int h = 0; h < 5; h++)
      {
        int p = personDistribution[t][h];
        if (p >= 0 && p < 6)placed[p] = true;
        else if (p >= PS_guest0 && p < PS_guestEnd)guestPlaced[p - PS_guest0] = true;
      }
    }
    vector<int> friends, cards, guests;
    for (int p = 0; p < 6; p++)
    {
      if (placed[p])continue;
      if (persons[p].personType == PersonType_card)cards.push_back(p);
      else if (persons[p].personType == PersonType_scenarioCard)friends.push_back(p);
    }
    for (int g = 0; g < mj_guestNum; g++)
      if (!guestPlaced[g])guests.push_back(PS_guest0 + g);
    std::shuffle(cards.begin(), cards.end(), *rand);
    std::shuffle(guests.begin(), guests.end(), *rand);
    mj_islandHouseNum = 0;
    for (int i = 0; i < 5; i++)
      mj_islandHouse[i] = -1;
    for (auto list : { &friends,&cards,&guests })
      for (int p : *list)
        if (mj_islandHouseNum < 5)
          mj_islandHouse[mj_islandHouseNum++] = p;
  }

  //列出所有参加者及其位置（理事长和记者不参加岛训练）
  int memberId[30];
  int memberPos[30];
  int memberNum = mj_islandMembers(memberId, memberPos);

  int supportAtFacility = 0, guestAtFacilityNum = 0, supportAtHouse = 0;
  int friendCount = 0;
  bool friendPosition[5] = { false,false,false,false,false };
  for (int m = 0; m < memberNum; m++)
  {
    int p = memberId[m];
    bool isSupport = p < 6;
    if (memberPos[m] == 5)
    {
      if (isSupport)supportAtHouse++;
    }
    else
    {
      if (isSupport)supportAtFacility++;
      else guestAtFacilityNum++;
      if (isSupport && isCardShining(p, memberPos[m]))
      {
        friendCount++;
        friendPosition[memberPos[m]] = true;
      }
    }
  }
  int supportNum = supportAtFacility + supportAtHouse;
  int friendPositionNum = 0;
  for (int t = 0; t < 5; t++)
    if (friendPosition[t])friendPositionNum++;
  mj_islandFriendCount = friendCount;
  mj_islandFriendPositions = friendPositionNum;

  //各支援卡的效果
  CardTrainingEffect effs[30];
  for (int m = 0; m < memberNum; m++)
  {
    int p = memberId[m];
    if (p >= 6)continue;
    const Person& ps = persons[p];
    int pos = memberPos[m];
    int atTrain = pos < 5 ? pos : (ps.cardParam.cardType < 5 ? ps.cardParam.cardType : 0);
    bool shining = pos < 5 && isCardShining(p, pos);
    effs[m] = ps.cardParam.getCardEffect(*this, shining, atTrain, ps.friendship, ps.cardRecord, supportNum, friendCount);
  }

  double countMultiplier = 1 + 0.05 * supportAtFacility + 0.02 * guestAtFacilityNum + 0.01 * supportAtHouse;
  double houseFriendMultiplier = (100 + friendCount * mj_trainingEffectByFriend()) / 100.0;
  double motivationBase = 0.1 * (motivation - 3);

  //多个设施同时友情时的训练效果（需要海之家）
  int friendPositionBonus = 0;
  if (mj_facilityLevel[MJ_house] > 0)
    friendPositionBonus = friendPositionNum >= 5 ? 80 : friendPositionNum == 4 ? 70 : friendPositionNum == 3 ? 65 : friendPositionNum == 2 ? 25 : 0;

  for (int s = 0; s < 6; s++)
  {
    double base = MJ_IslandBase[s] + mj_facilityIslandBonus(s);
    double friendMul = 1.0;
    double ganjing = 0;
    double xunlian = 0;
    for (int m = 0; m < memberNum; m++)
    {
      int p = memberId[m];
      if (p >= 6)continue;
      const CardTrainingEffect& eff = effs[m];
      double rate = MJ_IslandRate[memberPos[m]][s];
      base += int(eff.bonus[s]);
      if (memberPos[m] < 5 && isCardShining(p, memberPos[m]))
        friendMul *= (100 + int(eff.youQing * rate + 1e-6)) / 100.0;
      ganjing += int(eff.ganJing * rate + 1e-6);
      xunlian += int(eff.xunLian * rate + 1e-6);
    }
    double umaBonus = s < 5 ? 1 + 0.01 * fiveStatusBonus[s] : 1;
    double raw = base * umaBonus * friendMul * (1 + motivationBase * (1 + 0.01 * ganjing)) * (1 + 0.01 * xunlian) * countMultiplier * houseFriendMultiplier;
    int lower = int(raw + 0.0002);
    if (lower > 100)lower = 100;
    int upper = lower * (friendPositionBonus + mj_islandTrainingEffect(s)) / 100;
    if (upper > 100)upper = 100;
    mj_islandValueLower[s] = lower;
    if (s < 5)
    {
      lower = calculateRealStatusGain(fiveStatus[s], lower);
      upper = calculateRealStatusGain(fiveStatus[s] + lower, upper);
    }
    mj_islandValue[s] = lower + upper;
  }

  //发展pt：按 umasim，(60+参加人数×6)×(100+评价会加成+友情20)/100，参加人数含嘉宾和海之家
  if (mj_canGainPioneerPt())
    mj_islandPioneerPt = (60 + memberNum * 6) * (100 + mj_bonusPioneerPt + (friendCount > 0 ? 20 : 0)) / 100;
  else
    mj_islandPioneerPt = 0;
}

void Game::mj_applyIslandTraining(std::mt19937_64& rand)
{
  assert(mj_isIslandTrainingAvailable());
  stage = ST_event;
  mj_ticket -= 1;

  for (int i = 0; i < 5; i++)
    addStatus(i, mj_islandValue[i]);
  skillPt += mj_islandValue[5];

  //参加者（设施+海之家）。按 umasim，羁绊只给参加的支援卡：和训练一样再+3，即普通+10，友人+7
  int memberId[30];
  int memberPos[30];
  int memberNum = mj_islandMembers(memberId, memberPos);

  vector<int> hintCards;
  vector<int> trainedCards;
  bool clickFriend = false;
  for (int m = 0; m < memberNum; m++)
  {
    int p = memberId[m];
    if (p >= 6)continue;
    if (persons[p].personType == PersonType_card)
    {
      addJiBan(p, 10, 0);
      trainedCards.push_back(p);
      if (persons[p].isHint)
        hintCards.push_back(p);
    }
    else if (p == friend_personId && friend_type != 0)
    {
      addJiBan(p, 7, 0);
      clickFriend = true;
    }
  }
  if (hintCards.size() > 0)
  {
    int hintCard = hintCards[rand() % hintCards.size()];
    addJiBan(hintCard, 5, 1);
    addHintWithoutJiban(rand, hintCard);
  }

  if (mj_canGainPioneerPt())
    mj_addPioneerPt(mj_islandPioneerPt);

  if (clickFriend && friend_type != 0)
  {
    if (friend_type == 1)//SSR固有，岛训练时对所有参加的卡有效
      for (int p : trainedCards)
        mj_deyilvBonusNext[p] += 60;
    handleFriendClickEvent(rand, 0);
  }
  printEvents("岛训练");
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
    skillPt += int(5 * gameSettings.hintPtRate);//直線回復Lv5
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

  //出行后的选择：上是pt+3和发展pt（Lv50为80，Lv20为30，中间按线性估计），下只有pt+3
  //按 umasim 的选法：有券而且这些发展pt会让券溢出（券最多1张）时选下，否则选上
  skillPt += 3;
  int outingPt = 30 + (friend_level - 20) * 50 / 30;
  if (mj_canGainPioneerPt() && !mj_pioneerPtWouldWasteTicket(outingPt))
    mj_addPioneerPt(outingPt);
}

void Game::handleFriendFixedEvent()
{
  if (friend_type == 0)return;//没友人卡
  if (friend_stage < FriendStage_afterUnlockOutgoing)return;//没解锁出行就没事件
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
