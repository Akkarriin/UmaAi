#include <iostream>
#include <cassert>
#include <algorithm>
#include "Game.h"
using namespace std;

//剧本无关的通用流程。无人岛剧本的规则在Mujinto.cpp

const std::string Action::trainingName[9] =
{
  "速",
  "耐",
  "力",
  "根",
  "智",
  "休息",
  "外出",
  "比赛",
  "岛训练"
};

static bool randBool(mt19937_64& rand, double p)
{
  return rand() % 65536 < p * 65536;
}

//尽量与Game类的顺序一致
void Game::newGame(mt19937_64& rand, GameSettings settings, int newUmaId, int umaStars, int newCards[6], int newZhongMaBlueCount[5], int newZhongMaExtraBonus[6])
{
  gameSettings = settings;

  umaId = newUmaId;
  isLinkUma = GameConstants::isLinkChara(umaId);
  if (!GameDatabase::AllUmas.count(umaId))
  {
      cout << "\x1b[91m未知角色ID: " << umaId << ", 请更新AI数据" << "\x1b[0m" << endl;
      throw "ERROR Unknown character";
  }
  for (int i = 0; i < TOTAL_TURN; i++)
    isRacingTurn[i] = GameDatabase::AllUmas[umaId].races[i] == TURN_RACE;
  assert(isRacingTurn[11] == true);//出道赛
  isRacingTurn[TOTAL_TURN - 5] = true;//ura1
  isRacingTurn[TOTAL_TURN - 3] = true;//ura2
  isRacingTurn[TOTAL_TURN - 1] = true;//ura3

  for (int i = 0; i < 5; i++)
    fiveStatusBonus[i] = GameDatabase::AllUmas[umaId].fiveStatusBonus[i];

  turn = 0;
  stage = ST_distribute;
  vital = 100;
  maxVital = 100;
  motivation = 3;

  for (int i = 0; i < 5; i++)
    fiveStatus[i] = GameDatabase::AllUmas[umaId].fiveStatusInitial[i] - 10 * (5 - umaStars); //赛马娘初始值
  for (int i = 0; i < 5; i++)
    fiveStatusLimit[i] = GameConstants::BasicFiveStatusLimit[i]; //原始属性上限

  skillPt = 120;
  skillScore = umaStars >= 3 ? 170 * (umaStars - 2) : 120 * (umaStars);//固有技能
  hintSkillLvCount = 0;

  for (int i = 0; i < 5; i++)
  {
    trainLevelCount[i] = 0;
  }

  failureRateBias = 0;
  isQieZhe = false;
  isAiJiao = false;
  isPositiveThinking = false;
  isRefreshMind = false;

  haveCatchedDoll = false;

  for (int i = 0; i < 5; i++)
    zhongMaBlueCount[i] = newZhongMaBlueCount[i];
  for (int i = 0; i < 6; i++)
    zhongMaExtraBonus[i] = newZhongMaExtraBonus[i];

  for (int i = 0; i < 5; i++)
    fiveStatusLimit[i] += int(zhongMaBlueCount[i] * 5.34 * 2); //属性上限--种马基础值
  for (int i = 0; i < 5; i++)
    addStatus(i, zhongMaBlueCount[i] * 7); //种马

  stage = ST_distribute;
  decidingEvent = DecidingEvent_none;
  isRacing = false;

  friendship_noncard_yayoi = 0;
  friendship_noncard_reporter = 0;

  for (int i = 0; i < MAX_INFO_PERSON_NUM; i++)
  {
    persons[i] = Person();
  }

  saihou = 0;
  friend_type = 0;
  friend_personId = PS_none;
  friend_stage = FriendStage_notClicked;
  friend_outgoingNum = 0;
  friend_vitalBonus = 1.0;
  friend_statusBonus = 1.0;
  friend_level = 0;
  for (int i = 0; i < 6; i++)
  {
    int cardId = newCards[i];
    persons[i].setCard(cardId);
    saihou += persons[i].cardParam.saiHou;

    if (persons[i].personType == PersonType_scenarioCard)
    {
      friend_personId = i;
      bool isSSR = cardId / 10 == GameConstants::FriendCardIdSSR;
      friend_type = isSSR ? 1 : 2;
      int friendLimitBreak = cardId % 10;
      assert(friendLimitBreak >= 0 && friendLimitBreak <= 4);
      friend_level = (isSSR ? 30 : 20) + 5 * friendLimitBreak;//各突破的等级上限
      friend_vitalBonus = 1 + 0.01 * persons[i].cardParam.eventRecoveryAmountUp + 1e-10;//加个小量，避免因为舍入误差而算错
      friend_statusBonus = 1 + 0.01 * persons[i].cardParam.eventEffectUp + 1e-10;
    }
  }


  for (int i = 0; i < 6; i++)//支援卡初始加成
  {
    for (int j = 0; j < 5; j++)
      addStatus(j, persons[i].cardParam.initialBonus[j]);
    skillPt += persons[i].cardParam.initialBonus[5];
  }

  mj_init();

  randomizeTurn(rand); //随机分配卡组，包括计算属性
}

void Game::randomizeTurn(std::mt19937_64& rand)
{
  if (stage != ST_distribute)
    throw "随机分配卡组应当在ST_distribute";
  randomDistributeHeads(rand);

  //是否有hint。不在场的人头不需要考虑
  for (int i = 0; i < 6; i++)
    persons[i].isHint = false;
  if (!isRacing)
  {
    for (int t = 0; t < 5; t++)
    {
      bool alwaysHint = mj_alwaysHint(t);
      double hintRateUp = mj_hintRateUp(t);
      for (int h = 0; h < 5; h++)
      {
        int pid = personDistribution[t][h];
        if (pid < 0)break;
        if (pid >= 6)continue;

        if (persons[pid].personType == PersonType_card)
        {
          double hintProb = 0.075 * (1 + 0.01 * persons[pid].cardParam.hintProbIncrease) * (1 + 0.01 * hintRateUp);//不知道是加还是乘，先按乘算
          persons[pid].isHint = alwaysHint || randBool(rand, hintProb);
        }
      }
    }
    //没站位的卡在岛训练时可能去海之家，也可能有hint
    if (mj_ticket > 0)
    {
      for (int pid = 0; pid < 6; pid++)
      {
        if (persons[pid].personType != PersonType_card)continue;
        bool placed = false;
        for (int t = 0; t < 5; t++)
          for (int h = 0; h < 5; h++)
            if (personDistribution[t][h] == pid)placed = true;
        if (placed)continue;
        double hintProb = 0.075 * (1 + 0.01 * persons[pid].cardParam.hintProbIncrease) * (1 + 0.01 * mj_hintRateUp(persons[pid].cardParam.cardType));
        persons[pid].isHint = randBool(rand, hintProb);
      }
    }
  }

  calculateTrainingValue();
  stage = ST_train;
  mj_calculateIslandTraining(&rand);
}

void Game::undoRandomize()
{
  if (stage == ST_train)
    stage = ST_distribute;
}

//在probs的分布下给人头选择一个位置（0~4训练，5不在），满人或者冲突的训练重新抽
static int pickPosition(std::mt19937_64& rand, const std::vector<int>& probs, const int headN[5], const bool friendN[5], bool isFriendLike)
{
  auto distribution = std::discrete_distribution<>(probs.begin(), probs.end());
  while (true)
  {
    int atTrain = distribution(rand);
    if (atTrain < 5 && (headN[atTrain] >= 5 || (isFriendLike && friendN[atTrain])))
      continue; //训练满人或者有其他友人，重新分配
    return atTrain;
  }
}

void Game::randomDistributeHeads(std::mt19937_64& rand)
{
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 5; j++)
      personDistribution[i][j] = -1;

  //比赛回合
  if (isRacing)
  {
    return;//比赛不用分配卡组
  }

  int headN[5] = { 0,0,0,0,0 };
  bool friendN[5] = { 0,0,0,0,0 };
  auto place = [&](int p, int atTrain, bool isFriendLike)
    {
      if (atTrain >= 5)return;
      personDistribution[atTrain][headN[atTrain]] = p;
      headN[atTrain] += 1;
      if (isFriendLike)
        friendN[atTrain] = true;
    };

  int positionRateUp = mj_positionRateUp();

  //第一步：理事长/记者
  for (int p = PS_noncardYayoi; p <= PS_noncardReporter; p++)
  {
    if (isCampTraining())
      continue;
    if (p == PS_noncardReporter && turn < 12)//记者
      continue;
    std::vector<int> probs = { 100,100,100,100,100,200 }; //速耐力根智鸽
    place(p, pickPosition(rand, probs, headN, friendN, true), true);
  }

  //第二步：普通卡
  vector<int> normalCards;
  for (int card = 0; card < 6; card++)
  {
    if (persons[card].personType == PersonType_card)
      normalCards.push_back(card);
  }
  std::shuffle(normalCards.begin(), normalCards.end(), rand);//保证所有卡的地位是公平的

  for (int t = 0; t < normalCards.size(); t++)
  {
    int p = normalCards[t];
    auto& ps = persons[p];
    int cardType = ps.cardParam.cardType;
    int bonus = mj_specialtyRateUp(cardType) + mj_deyilvBonus[p];
    int mainRate = int((100 + ps.cardParam.deYiLv) * (100 + bonus) / 100);
    std::vector<int> probs = { 100,100,100,100,100,50 * (100 - positionRateUp) / 100 }; //速耐力根智鸽
    probs[cardType] = mainRate;
    place(p, pickPosition(rand, probs, headN, friendN, false), false);
  }

  //第三步：嘉宾，没有得意训练
  for (int g = 0; g < mj_guestNum; g++)
  {
    std::vector<int> probs = { 100,100,100,100,100,50 * (100 - positionRateUp) / 100 };
    place(PS_guest0 + g, pickPosition(rand, probs, headN, friendN, false), false);
  }

  //第四步：友人卡（塔克布莱恩第3回合才参加）
  for (int p = 0; p < 6; p++)
  {
    auto& ps = persons[p];
    if (ps.personType == PersonType_card)continue;
    if (ps.personType == PersonType_scenarioCard && turn < 2)continue;
    std::vector<int> probs = { 100,100,100,100,100,100 - positionRateUp }; //速耐力根智鸽
    place(p, pickPosition(rand, probs, headN, friendN, true), true);
  }
}

void Game::calculateTrainingValue()
{
  for (int i = 0; i < 5; i++)
    calculateTrainingValueSingle(i);
}
void Game::addTrainingLevelCount(int trainIdx, int n)
{
  trainLevelCount[trainIdx] += n;
  if (trainLevelCount[trainIdx] > 16)trainLevelCount[trainIdx] = 16;
}
int Game::calculateRealStatusGain(int value, int gain) const//考虑1200以上为2的倍数的实际属性增加值
{
  int newValue = value + gain;
  if (newValue <= 1200)return gain;
  if (gain == 1)return 2;
  return (newValue / 2) * 2 - value;
}
void Game::addStatus(int idx, int value)
{
  assert(idx >= 0 && idx < 5);
  int t = fiveStatus[idx] + value;

  if (t > fiveStatusLimit[idx])
    t = fiveStatusLimit[idx];
  if (t < 1)
    t = 1;
  if (t > 1200)
    t = (t / 2) * 2;
  fiveStatus[idx] = t;
}
void Game::addVital(int value)
{
  vital += value;
  if (vital > maxVital)
    vital = maxVital;
  if (vital < 0)
    vital = 0;
}
void Game::addVitalMax(int value)
{
  maxVital += value;
  if (maxVital > 120)
    maxVital = 120;
}
void Game::addMotivation(int value)
{
  if (value < 0)
  {
    if (isPositiveThinking)
      isPositiveThinking = false;
    else
    {
      motivation += value;
      if (motivation < 1)
        motivation = 1;
    }
  }
  else
  {
    motivation += value;
    if (motivation > 5)
      motivation = 5;
  }
}
void Game::addJiBan(int idx, int value, int type) //type0是点击，type1是事件，type2是没有任何羁绊加成
{
  if (idx == PS_noncardYayoi)
    friendship_noncard_yayoi += value;
  else if (idx == PS_noncardReporter)
    friendship_noncard_reporter += value;
  else if (idx >= 0 && idx < 6)
  {
    int gain = value;
    if (type == 0 || type == 1)
    {
      if (isAiJiao)
        gain += 2;
    }
    auto& p = persons[idx];
    p.friendship += gain;
    if (p.friendship > 100)p.friendship = 100;
  }
  //嘉宾不记录羁绊
}
void Game::addAllStatus(int value)
{
  for (int i = 0; i < 5; i++)addStatus(i, value);
}
int Game::calculateFailureRate(int trainType, double failRateMultiply) const
{
  //粗略拟合的训练失败率，二次函数 A*(x0-x)^2+B*(x0-x)
  //误差应该在2%以内
  double x0 = 0.1 * GameConstants::FailRateBasic[trainType][getTrainingLevel(trainType)];

  double f = 0;
  if (vital < x0)
  {
    f = (100 - vital) * (x0 - vital) / 40.0;
  }
  if (f < 0)f = 0;
  if (f > 99)f = 99;//无练习下手，失败率最高99%
  f *= failRateMultiply;//支援卡的训练失败率下降词条
  int fr = ceil(f);
  fr += failureRateBias;
  if (fr < 0)fr = 0;
  if (fr > 100)fr = 100;
  return fr;
}
void Game::runRace(int basicFiveStatusBonus, int basicPtBonus)
{
  double raceMultiply = 1 + 0.01 * saihou;

  int fiveStatusBonus = int(raceMultiply * basicFiveStatusBonus);
  int ptBonus = int(raceMultiply * basicPtBonus);
  addAllStatus(fiveStatusBonus);
  skillPt += ptBonus;
}

void Game::addStatusFriend(int idx, int value)
{
  value = int(value * friend_statusBonus);
  if (idx == 5)skillPt += value;
  else addStatus(idx, value);
}

void Game::addVitalFriend(int value)
{
  value = int(value * friend_vitalBonus);
  addVital(value);
}

void Game::handleOutgoing(std::mt19937_64& rand)
{
  assert(stage == ST_train);
  stage = ST_event;
  if (isXiahesu())
  {
    addVital(40);
    addMotivation(1);
    if (failureRateBias > 0)failureRateBias = 0;//治练习下手
  }
  else if (friend_type != 0 &&  //带了友人卡
    friend_stage == FriendStage_afterUnlockOutgoing &&  //已解锁外出
    friend_outgoingNum < 5  //外出没走完
    )
  {
    //等待选择友人出行
    stage = ST_decideEvent;
    decidingEvent = DecidingEvent_outing;
  }
  else //普通出行
  {
    runNormalOutgoing(rand);
  }
}

void Game::runNormalOutgoing(std::mt19937_64& rand)
{
  //懒得查概率了，就50%加2心情，50%加1心情10体力
  if (rand() % 2)
    addMotivation(2);
  else
  {
    addMotivation(1);
    addVital(10);
  }

  //抓娃娃
  if (turn >= 24 && (!haveCatchedDoll) && (rand() % 3 == 0))
  {
    addVital(15);
    addMotivation(1);
    haveCatchedDoll = true;
  }
}

bool Game::applyTraining(std::mt19937_64& rand, int16_t train)
{
  assert(stage == ST_train);

  if (isRacing)
  {
    //固定比赛收益在checkFixedEvents()里处理
    stage = ST_event;
    assert(train == T_race);
    return true;
  }

  if (train == T_rest)//休息
  {
    if (isXiahesu())//合宿只能外出
    {
      return false;
    }
    stage = ST_event;
    int r = rand() % 100;
    if (r < 25)
      addVital(70);
    else if (r < 82)
      addVital(50);
    else
      addVital(30);
  }
  else if (train == T_race)//比赛
  {
    if (!isRaceAvailable())
    {
      printEvents("Cannot race now.");
      return false;
    }
    stage = ST_event;
    runRace(2, 30);//粗略的近似

    //扣体固定15
    addVital(-15);
    if (rand() % 5 == 0)
      addMotivation(1);

    if (mj_canGainPioneerPt())
      mj_addPioneerPt(mj_calcRacePioneerPt(false));
  }
  else if (train == T_outgoing)//外出
  {
    handleOutgoing(rand);
  }
  else if (train == T_island)//岛训练
  {
    if (!mj_isIslandTrainingAvailable())
      return false;
    mj_applyIslandTraining(rand);
  }
  else if (train <= 4 && train >= 0)//常规训练
  {
    bool trainingSucceed = !(rand() % 100 < failRate[train]);
    applyNormalTraining(rand, train, trainingSucceed);
  }
  else
  {
    printEvents("未知的训练项目");
    return false;
  }
  return true;
}

void Game::applyNormalTraining(std::mt19937_64& rand, int16_t train, bool success)
{
  assert(stage == ST_train);
  stage = ST_event;
  if (!success)
  {
    if (failRate[train] >= 20 && (rand() % 100 < failRate[train]))//训练大失败，概率是瞎猜的
    {
      printEvents("训练大失败！");
      addStatus(train, -10);
      if (fiveStatus[train] > 1200)
        addStatus(train, -10);//游戏里1200以上扣属性不折半，在此模拟器里对应1200以上翻倍
      //随机扣2个10，不妨改成全属性-4降低随机性
      for (int i = 0; i < 5; i++)
      {
        addStatus(i, -4);
        if (fiveStatus[i] > 1200)
          addStatus(i, -4);//游戏里1200以上扣属性不折半，在此模拟器里对应1200以上翻倍
      }
      addMotivation(-3);
      addVital(10);
    }
    else//小失败
    {
      printEvents("训练小失败！");
      addStatus(train, -5);
      if (fiveStatus[train] > 1200)
        addStatus(train, -5);//游戏里1200以上扣属性不折半，在此模拟器里对应1200以上翻倍
      addMotivation(-1);
    }
    return;
  }

  //先加上训练值
  for (int i = 0; i < 5; i++)
    addStatus(i, trainValue[train][i]);
  skillPt += trainValue[train][5];
  addVital(trainVitalChange[train]);

  bool clickFriend = false;
  vector<int> hintCards;//有哪几个卡出红感叹号了
  vector<int> trainedCards;//一起训练的普通卡

  for (int i = 0; i < 5; i++)
  {
    int p = personDistribution[train][i];
    if (p < 0)break;//没人

    if (p == friend_personId && friend_type != 0)//友人卡
    {
      assert(persons[p].personType == PersonType_scenarioCard);
      addJiBan(p, 4, 0);
      clickFriend = true;
    }
    else if (p < 6)//普通卡
    {
      addJiBan(p, 7, 0);
      trainedCards.push_back(p);
      if (persons[p].isHint)
        hintCards.push_back(p);
    }
    else if (p == PS_noncardYayoi)//非卡理事长
    {
      int jiban = friendship_noncard_yayoi;
      int g = jiban < 40 ? 2 : jiban < 60 ? 3 : jiban < 80 ? 4 : 5;
      skillPt += g;
      addJiBan(PS_noncardYayoi, 7, 0);
    }
    else if (p == PS_noncardReporter)//记者
    {
      int jiban = friendship_noncard_reporter;
      int g = jiban < 40 ? 2 : jiban < 60 ? 3 : jiban < 80 ? 4 : 5;
      addStatus(train, g);
      addJiBan(PS_noncardReporter, 7, 0);
    }
    //嘉宾没有额外效果
  }

  if (hintCards.size() > 0)
  {
    int hintCard = hintCards[rand() % hintCards.size()];//随机一张卡出hint
    addJiBan(hintCard, 5, 1);
    addHintWithoutJiban(rand, hintCard);
  }

  //训练等级提升
  if (!isCampTraining())
    addTrainingLevelCount(train, 1);

  //发展pt
  if (mj_canGainPioneerPt())
    mj_addPioneerPt(mj_trainPioneerPt[train]);

  if (clickFriend)
  {
    //SSR塔克布莱恩固有：一起训练的其他支援卡下回合得意率+60
    if (friend_type == 1)
    {
      for (int p : trainedCards)
        mj_deyilvBonusNext[p] += 60;
    }
    handleFriendClickEvent(rand, train);
  }
}
void Game::addHintWithoutJiban(std::mt19937_64& rand, int idx)
{
  int hintLevel = persons[idx].cardParam.hintLevel;
  int cardType = persons[idx].cardParam.cardType;
  assert(cardType < 5 && cardType >= 0);
  double skillProb = 0.8;
  if (hintLevel == 0)skillProb = 0;//根乌拉拉这种，只给属性

  if (randBool(rand, skillProb))
  {
    hintSkillLvCount += hintLevel;

    skillPt += int(hintLevel * gameSettings.hintPtRate);
  }
  else
  {
    if (cardType == 0)
    {
      addStatus(0, 6);
      addStatus(2, 2);
    }
    else if (cardType == 1)
    {
      addStatus(1, 6);
      addStatus(3, 2);
    }
    else if (cardType == 2)
    {
      addStatus(2, 6);
      addStatus(1, 2);
    }
    else if (cardType == 3)
    {
      addStatus(3, 6);
      addStatus(0, 1);
      addStatus(2, 1);
    }
    else if (cardType == 4)
    {
      addStatus(4, 6);
      skillPt += 5;
    }
    else
      throw "友人团队卡不能hint";
  }
}
void Game::addRandomCardHint(std::mt19937_64& rand)
{
  vector<int> cards;
  for (int i = 0; i < 6; i++)
    if (persons[i].personType == PersonType_card)
      cards.push_back(i);
  if (cards.size() == 0)return;
  addHintWithoutJiban(rand, cards[rand() % cards.size()]);
}
void Game::jicheng(std::mt19937_64& rand)
{
  for (int i = 0; i < 5; i++)
    addStatus(i, zhongMaBlueCount[i] * 6); //蓝因子典型值

  double factor = double(rand() % 65536) / 65536 * 2;//剧本因子随机0~2倍
  for (int i = 0; i < 5; i++)
    addStatus(i, int(factor * zhongMaExtraBonus[i])); //剧本因子
  skillPt += int((0.5 + 0.5 * factor) * zhongMaExtraBonus[5]);//乱七八糟技能的等效pt

  for (int i = 0; i < 5; i++)
    fiveStatusLimit[i] += zhongMaBlueCount[i] * 2; //属性上限--种马基础值。18蓝两次继承共加大约36上限，每次每个蓝因子+1上限，1200折半再乘2

  for (int i = 0; i < 5; i++)
    fiveStatusLimit[i] += rand() % 8; //属性上限--后两次继承随机增加

}

void Game::decideEvent(std::mt19937_64& rand, int16_t idx)
{
  if (decidingEvent != DecidingEvent_outing)
    throw "unknown decidingEvent";
  if (friend_type == 0 || friend_stage != FriendStage_afterUnlockOutgoing || friend_outgoingNum >= 5 || isXiahesu())
    throw "decideEvent友人外出不可用";

  if (idx == 0)
    runNormalOutgoing(rand);
  else if (idx == 1)
    runFriendOutgoing(rand, true);
  else if (idx == 2)
  {
    if (friend_outgoingNum != 2)
      throw "只有第3段友人出行有下选项";
    runFriendOutgoing(rand, false);
  }
  else throw "未知的出行选项";

  decidingEvent = DecidingEvent_none;
  stage = ST_event;
}


bool Game::isLegal(Action action) const
{
  if (action.stage != ST_train)
    return false;
  if (isRacing)
  {
    return action.idx == T_race;
  }

  if (action.idx == T_rest)
  {
    if (isXiahesu())
    {
      return false;//将夏合宿的“外出&休息”称为外出
    }
    return true;
  }
  else if (action.idx == T_outgoing)
  {
    return true;
  }
  else if (action.idx == T_race)
  {
    return isRaceAvailable();
  }
  else if (action.idx >= 0 && action.idx <= 4)
  {
    return true;
  }
  else if (action.idx == T_island)
  {
    return mj_isIslandTrainingAvailable();
  }
  return false;
}



float Game::getSkillScore() const
{
  float rate = isQieZhe ? gameSettings.ptScoreRate * 1.1 : gameSettings.ptScoreRate ;
  return rate * skillPt + skillScore;
}

static double scoringFactorOver1200(double x)//耐力胜负，脚色十分，追比
{
  if (x <= 1150)return 0;
  return tanh((x - 1150) / 100.0) * sqrt(x - 1150);
}

static double realRacingStatus(double x)
{
  if (x < 1200)return x;
  return 1200 + (x - 1200) / 4;
}

static double smoothUpperBound(double x)
{
  return (x - sqrt(x * x + 1)) / 2;
}

int Game::finalScore_mile() const
{
  double weights[5] = { 400,300,70,70,120 };
  double weights1200[5] = { 0,0,20,10,0 };


  double staminaTarget = 900;
  double staminaBonus = 5 * 100 * (smoothUpperBound((realRacingStatus(fiveStatus[1]) - staminaTarget) / 100.0) - smoothUpperBound((0 - staminaTarget) / 100.0));

  double total = 0;
  total += staminaBonus;
  for (int i = 0; i < 5; i++)
  {
    double realStat = realRacingStatus(min(fiveStatus[i], fiveStatusLimit[i]));
    total += weights[i] * sqrt(realStat);
    total += weights1200[i] * scoringFactorOver1200(realStat);
  }

  total += getSkillScore();
  if (total < 0)total = 0;
  return (int)total;
}

int Game::finalScore_sum() const
{
  double weights[5] = { 5,3,3,3,3 };
  double total = 0;
  for (int i = 0; i < 5; i++)
  {
    double realStat = min(fiveStatus[i], fiveStatusLimit[i]);
    if (realStat > 1200)realStat = 1200 + (realStat - 1200) / 2;
    total += weights[i] * realStat;
  }

  total += getSkillScore();
  if (total < 0)total = 0;
  return (int)total;
}

int Game::finalScore_rank() const
{
  int total = 0;
  for (int i = 0; i < 5; i++)
    total += GameConstants::FiveStatusFinalScore[min(fiveStatus[i], fiveStatusLimit[i])];

  total += int(getSkillScore());
  return total;
}

int Game::finalScore() const
{
  if (gameSettings.scoringMode == SM_normal)
  {
    return finalScore_rank();
  }
  else if (gameSettings.scoringMode == SM_race)
  {
    return finalScore_sum();
  }
  else if (gameSettings.scoringMode == SM_mile)
  {
    return finalScore_mile();
  }
  else
  {
    throw "此评分算法还未实现";
  }
  return 0;
}

bool Game::isEnd() const
{
  return turn >= TOTAL_TURN;
}

int Game::getTrainingLevel(int item) const
{
  if (isCampTraining())return 4;

  return trainLevelCount[item] / 4;
}

void Game::calculateTrainingValueSingle(int tra)
{
  int headNum = 0;//几张卡或者嘉宾，理事长记者不算
  int shiningNum = 0;//几张闪彩

  int basicValue[6] = { 0,0,0,0,0,0 };//训练的基础值，=原基础值+支援卡加成

  double totalXunlian = 0;//训练1+训练2+...
  double totalGanjing = 0;//干劲1+干劲2+...
  double totalYouqingMultiplier = 1.0;//(1+友情1)*(1+友情2)*...
  int vitalCostBasic;//体力消耗基础量，=ReLU(基础体力消耗+link体力消耗增加-智彩体力消耗减少)
  double vitalCostMultiplier = 1.0;//(1-体力消耗减少率1)*(1-体力消耗减少率2)*...
  double failRateMultiplier = 1.0;//(1-失败率下降率1)*(1-失败率下降率2)*...

  bool camp = isCampTraining();
  int tlevel = getTrainingLevel(tra);

  for (int h = 0; h < 5; h++)
  {
    int pIdx = personDistribution[tra][h];
    if (pIdx < 0)break;
    if (pIdx == PS_noncardYayoi || pIdx == PS_noncardReporter)continue;//不是卡

    headNum += 1;
    if (isCardShining(pIdx, tra))
      shiningNum += 1;
  }
  trainShiningNum[tra] = shiningNum;
  trainHeadNum[tra] = headNum;

  //基础值
  if (camp)
  {
    //岛合宿：设施建成后0的位置变成1或2，设施的属性加成加在原本不为0的属性上（pt总是不为0）
    const int* campValue = GameConstants::MJ_CampTrainingValue[mj_facilityLevel[tra] > 0 ? 1 : 0][tra];
    for (int i = 0; i < 6; i++)
    {
      basicValue[i] = campValue[i];
      if (basicValue[i] > 0)
        basicValue[i] += mj_facilityIslandBonus(i);
    }
    vitalCostBasic = -campValue[6];
  }
  else
  {
    for (int i = 0; i < 6; i++)
      basicValue[i] = GameConstants::TrainingBasicValue[tra][tlevel][i];
    vitalCostBasic = -GameConstants::TrainingBasicValue[tra][tlevel][6];
  }

  for (int h = 0; h < 5; h++)
  {
    int pid = personDistribution[tra][h];
    if (pid < 0)break;//没人
    if (pid >= 6)continue;//理事长记者嘉宾，没有支援卡加成

    const Person& p = persons[pid];
    bool isThisCardShining = isCardShining(pid, tra);//这张卡闪没闪
    CardTrainingEffect eff = p.cardParam.getCardEffect(*this, isThisCardShining, tra, p.friendship, p.cardRecord, headNum, shiningNum);

    for (int i = 0; i < 6; i++)//基础值bonus
    {
      if (basicValue[i] > 0)
        basicValue[i] += int(eff.bonus[i]);
    }
    if (isThisCardShining)//闪彩，友情加成和智彩回复
    {
      totalYouqingMultiplier *= (1 + 0.01 * eff.youQing);
      if (tra == T_wiz)
        vitalCostBasic -= eff.vitalBonus;
    }
    totalXunlian += eff.xunLian;
    totalGanjing += eff.ganJing;
    vitalCostMultiplier *= (1 - 0.01 * eff.vitalCostDrop);
    failRateMultiplier *= (1 - 0.01 * eff.failRateDrop);
  }

  //体力，失败率
  int vitalChangeInt = vitalCostBasic > 0 ? -int(vitalCostBasic * vitalCostMultiplier) : -vitalCostBasic;
  if (vitalChangeInt > maxVital - vital)vitalChangeInt = maxVital - vital;
  if (vitalChangeInt < -vital)vitalChangeInt = -vital;
  trainVitalChange[tra] = vitalChangeInt;
  failRate[tra] = calculateFailureRate(tra, failRateMultiplier);

  //下层 = 基础值 * 人头 * 训练 * 干劲 * 友情 * 马娘成长率，岛合宿时还要乘海之家的友情人数加成
  double motivationFactor = 0.1 * (motivation - 3);
  double multiplierLower = (1 + 0.05 * headNum) * (1 + 0.01 * totalXunlian) * (1 + motivationFactor * (1 + 0.01 * totalGanjing)) * totalYouqingMultiplier;
  if (camp)
    multiplierLower *= (100 + shiningNum * mj_trainingEffectByFriend()) / 100.0;

  //上层 = 下层 * 剧本训练效果（平时是评价会加成，合宿是设施加成）
  int upperEffect = camp ? mj_campTrainingEffect(tra) : mj_bonusTrainingEffect;

  for (int i = 0; i < 6; i++)
  {
    double umaBonus = i < 5 ? 1 + 0.01 * fiveStatusBonus[i] : 1;
    int lower = int(basicValue[i] * multiplierLower * umaBonus + 1e-4);
    if (lower > 100) lower = 100;
    int upper = lower * upperEffect / 100;
    if (upper > 100)upper = 100;
    trainValueLower[tra][i] = lower;
    if (i < 5)
    {
      lower = calculateRealStatusGain(fiveStatus[i], lower);//consider the integer over 1200
      upper = calculateRealStatusGain(fiveStatus[i] + lower, upper);
    }
    trainValue[tra][i] = upper + lower;
  }

  mj_trainPioneerPt[tra] = mj_calcTrainingPioneerPt(headNum, shiningNum > 0);
}

void Game::checkEvent(std::mt19937_64& rand)
{
  assert(stage == ST_event);
  checkFixedEvents(rand);
  checkRandomEvents(rand);

  //回合数+1
  turn++;
  stage = ST_distribute;

  //塔克布莱恩给的得意率只持续到下一回合
  for (int i = 0; i < 6; i++)
  {
    mj_deyilvBonus[i] = mj_deyilvBonusNext[i];
    mj_deyilvBonusNext[i] = 0;
  }

  if (turn >= TOTAL_TURN)
  {
    printEvents("育成结束!");
    printEvents("你的得分是：" + to_string(finalScore()));
  }
  else {
    isRacing = isRacingTurn[turn];
  }
}

void Game::checkFixedEvents(std::mt19937_64& rand)
{
  //处理各种固定事件
  if (isRefreshMind)
  {
    addVital(5);
    if (rand() % 4 == 0) //假设每回合有25%概率buff消失
      isRefreshMind = false;
  }
  if (isRacing)//生涯比赛
  {
    if (turn < 72)
    {
      runRace(3, 45);
      addJiBan(PS_noncardYayoi, 4, 1);
      if (mj_canGainPioneerPt())
        mj_addPioneerPt(mj_calcRacePioneerPt(true));
    }
    else if (turn == 73)//ura1
    {
      runRace(10, 40);
    }
    else if (turn == 75)//ura2
    {
      runRace(10, 60);
    }
    else if (turn == 77)//ura3
    {
      runRace(10, 80);
    }
  }

  //无人岛：建设计划与评价会，在第2,12,24,36,48,60回合行动后
  if (turn == 1 || turn == 11 || turn == 23 || turn == 35 || turn == 47 || turn == 59)
    mj_evaluationAndPlan(rand, (turn + 1) / 12);

  if (turn == 11)//出道赛
  {
    assert(isRacing);
  }
  else if (turn == 23)//第一年年底
  {
    handleFriendFixedEvent();
    //年底事件，体力低选择体力，否则选属性
    if (maxVital - vital >= 20)
      addVital(20);
    else
      addAllStatus(5);
    printEvents("第一年结束");
  }
  else if (turn == 29)//第二年继承
  {
    jicheng(rand);
    printEvents("第二年继承");
  }
  else if (turn == 35)
  {
    printEvents("第二年合宿开始");
  }
  else if (turn == 39)//经典年合宿结束，补建合宿期间达到条件的设施
  {
    mj_upgradeAfterCamp();
  }
  else if (turn == 47)//第二年年底
  {
    //年底事件，体力低选择体力，否则选属性
    if (maxVital - vital >= 30)
      addVital(30);
    else
      addAllStatus(8);
    printEvents("第二年结束");
  }
  else if (turn == 48)//抽奖
  {
    int rd = rand() % 100;
    if (rd < 16)//温泉或一等奖
    {
      addVital(30);
      addAllStatus(10);
      addMotivation(2);
      printEvents("抽奖：你抽中了温泉/一等奖");
    }
    else if (rd < 16 + 27)//二等奖
    {
      addVital(20);
      addAllStatus(5);
      addMotivation(1);
      printEvents("抽奖：你抽中了二等奖");
    }
    else if (rd < 16 + 27 + 46)//三等奖
    {
      addVital(20);
      printEvents("抽奖：你抽中了三等奖");
    }
    else//厕纸
    {
      addMotivation(-1);
      printEvents("抽奖：你抽中了厕纸");
    }
  }
  else if (turn == 49)
  {
    skillScore += 170;
    printEvents("固有等级+1");
  }
  else if (turn == 53)//第三年继承
  {
    jicheng(rand);
    printEvents("第三年继承");

    if (friendship_noncard_yayoi >= 60)
    {
      skillScore += 170;//固有技能等级+1
      addMotivation(1);
    }
    else
    {
      addVital(-5);
      skillPt += 25;
    }
  }
  else if (turn == 59)
  {
    printEvents("第三年合宿开始");
  }
  else if (turn == 63)//资深年合宿结束，岛训练券+2
  {
    mj_ticket += 2;
    printEvents("岛训练券+2");
  }
  else if (turn == 64)//剧本金技能（塔克布莱恩：アガッてきた！）
  {
    skillPt += 50;
  }
  else if (turn == 70)
  {
    skillScore += 170;//固有技能等级+1
  }
  else if (turn == TOTAL_TURN - 1)//ura3，游戏结束
  {
    //记者
    if (friendship_noncard_reporter >= 80)
    {
      addAllStatus(5);
      skillPt += 20;
    }
    else if (friendship_noncard_reporter >= 60)
    {
      addAllStatus(3);
      skillPt += 10;
    }
    else if (friendship_noncard_reporter >= 40)
    {
      skillPt += 10;
    }
    else
    {
      skillPt += 5;
    }

    //无人岛结算：全部大好评+55/540（本能の懸け橋），否则+45/420（あるがままに）
    bool allGreat = true;
    for (int i = 1; i <= 5; i++)
      if (mj_evalResult[i] != 2)
        allGreat = false;
    if (allGreat)
    {
      addAllStatus(55);
      skillPt += 540;
    }
    else
    {
      addAllStatus(45);
      skillPt += 420;
    }

    //友人卡事件
    handleFriendFixedEvent();

    printEvents("结束，游戏结算");
  }
}

void Game::checkRandomEvents(std::mt19937_64& rand)
{
  if (turn >= 72)
    return;//ura期间不会发生各种随机事件

  //友人会不会解锁出行
  if (friend_type != 0 && friend_stage == FriendStage_beforeUnlockOutgoing)
  {
    Person& p = persons[friend_personId];
    double unlockOutgoingProb = p.friendship >= 60 ?
      GameConstants::FriendUnlockOutgoingProbEveryTurnHighFriendship :
      GameConstants::FriendUnlockOutgoingProbEveryTurnLowFriendship;
    if (randBool(rand, unlockOutgoingProb))//启动
    {
      handleFriendUnlock(rand);
    }
  }

  //模拟各种随机事件

  //支援卡连续事件，随机给一个卡加5羁绊
  if (!isXiahesu() && randBool(rand, GameConstants::EventProb))
  {
    int card = rand() % 6;
    addJiBan(card, 5, 1);
    //前期事件属性/技能少，后期多
    double statusToGain = gameSettings.eventStrength * (0.5 + 1.0 * (turn * 1.0 / TOTAL_TURN));
    addStatus(rand() % 5, statusToGain);
    skillPt += statusToGain * 0.5;
    printEvents("模拟支援卡随机事件：" + persons[card].cardParam.cardName + " 的羁绊+5，pt和随机属性+" + to_string(gameSettings.eventStrength));

    //支援卡一般是前几个事件加心情
    if (randBool(rand, 0.6 * pow((1.0 - turn * 1.0 / TOTAL_TURN), 2)))
    {
      addMotivation(1);
      printEvents("模拟支援卡随机事件：心情+1");
    }
    if (randBool(rand, 0.5))
    {
      addVital(10);
      printEvents("模拟支援卡随机事件：体力+10");
    }
    else if (randBool(rand, 0.003))
    {
      addVital(-10);
      printEvents("模拟支援卡随机事件：体力-10");
    }
    if (randBool(rand, 0.003))
    {
      isPositiveThinking = true;
      printEvents("模拟支援卡随机事件：获得“正向思考”");
    }
  }

  //模拟马娘随机事件
  if (turn >= 24 && randBool(rand, 0.25))
  {
    addAllStatus(3);
    skillPt += 15;
    printEvents("模拟马娘随机事件：全属性+3");
  }
  if (turn >= 48 && randBool(rand, 0.25))
  {
    addAllStatus(3);
    skillPt += 15;
    printEvents("模拟马娘随机事件：全属性+3");
  }

  //加体力
  if (randBool(rand, 0.10))
  {
    addVital(5);
    printEvents("模拟随机事件：体力+5");
  }

  //加体力
  if (randBool(rand, 0.05))
  {
    addVital(10);
    printEvents("模拟随机事件：体力+10");
  }

  //加30体力（吃饭事件）
  if (randBool(rand, 0.02))
  {
    addVital(30);
    printEvents("模拟随机事件：体力+30");
  }

  //加心情
  if (randBool(rand, 0.02))
  {
    addMotivation(1);
    printEvents("模拟随机事件：心情+1");
  }

  //掉心情
  if (turn >= 12 && randBool(rand, 0.06))
  {
    addMotivation(-1);
    printEvents("模拟随机事件：\033[0m\033[33m心情-1\033[0m\033[32m");
  }

}
std::vector<Action> Game::getAllLegalActions() const
{
  std::vector<Action> allActions;

  if (isEnd()) return allActions;

  if (stage == ST_distribute)
  {
    allActions.push_back(Action(ST_distribute));
  }
  else if (stage == ST_train)
  {
    if (isRacing)
    {
      allActions.push_back(Action(ST_train, T_race));
      return allActions;
    }
    for (int i = 0; i < 5; i++)
      allActions.push_back(Action(ST_train, i));
    if(!isXiahesu())
      allActions.push_back(Action(ST_train, T_rest));
    allActions.push_back(Action(ST_train, T_outgoing));
    if (isRaceAvailable())
      allActions.push_back(Action(ST_train, T_race));
    if (mj_isIslandTrainingAvailable())
      allActions.push_back(Action(ST_train, T_island));
  }
  else if (stage == ST_decideEvent)
  {
    if (decidingEvent == DecidingEvent_outing)
    {
      if (!(friend_stage == FriendStage_afterUnlockOutgoing && friend_outgoingNum < 5))
        throw "无法友人出行";
      allActions.push_back(Action(ST_decideEvent, 0));//普通出行
      allActions.push_back(Action(ST_decideEvent, 1));//友人出行
      if (friend_outgoingNum == 2)
        allActions.push_back(Action(ST_decideEvent, 2));//第3段的下选项
    }
    else throw "未知decidingEvent";
  }
  else if (stage == ST_event)
  {
    allActions.push_back(Action(ST_event));
  }
  else
    throw "未知stage";
  return allActions;
}
void Game::applyAction(std::mt19937_64& rand, Action action)
{
  if (isEnd()) return;
  if (action.stage == ST_action_randomize)
  {
    undoRandomize();
    return;
  }
  if (stage != action.stage)
    throw "Game::applyAction的stage不匹配";

  if (stage == ST_distribute)
  {
    randomizeTurn(rand);
  }
  else if (stage == ST_train)
  {
    bool suc = applyTraining(rand, action.idx);
    assert(suc && "Game::applyAction选择了不合法的训练");
  }
  else if (stage == ST_decideEvent)
  {
    decideEvent(rand, action.idx);
  }
  else if (stage == ST_event)
  {
    checkEvent(rand);
  }
  else
    throw "未知stage";
}

void Game::continueUntilNextDecision(std::mt19937_64& rand)
{
  while (true)
  {
    auto allActions = getAllLegalActions();
    if (allActions.size() == 0)
    {
      assert(isEnd());
      return;
    }
    else if (allActions.size() == 1)
      applyAction(rand, allActions[0]);
    else return;
  }
}

void Game::applyActionUntilNextDecision(std::mt19937_64& rand, Action action)
{
  applyAction(rand, action);
  continueUntilNextDecision(rand);
}

bool Game::isCardShining(int personIdx, int trainIdx) const
{
  if (personIdx >= 0 && personIdx < 6)
  {
    const Person& p = persons[personIdx];
    if (p.personType == PersonType_card)
    {
      return p.friendship >= 80 && trainIdx == p.cardParam.cardType;
    }
    return false;//塔克布莱恩没有友情训练
  }
  return false;//理事长、记者、嘉宾
}

GameSettings::GameSettings()
{
  playerPrint = false;
  ptScoreRate = GameConstants::ScorePtRateDefault;
  hintPtRate = GameConstants::HintLevelPtRateDefault;
  hintProbTimeConstant= GameConstants::HintProbTimeConstantDefault;
  eventStrength = GameConstants::EventStrengthDefault;
  scoringMode = SM_normal;
}

Action::Action():stage(ST_none),idx(0)
{
}

Action::Action(int st) :stage(st), idx(0)
{
}

Action::Action(int st, int idx):stage(st), idx(idx)
{
}

std::string Action::toString() const
{
  return "Stage"+to_string(stage) + "_Idx" + to_string(idx);
}

std::string Action::toString(const Game& game) const
{
  if (idx < 0)
    return toString();
  if (stage == ST_train)
  {
    return trainingName[idx];
  }
  else if (stage == ST_decideEvent)
  {
    if (game.decidingEvent == DecidingEvent_outing)
    {
      if (idx == 0)
        return "普通外出";
      string s = "友人外出" + to_string(game.friend_outgoingNum + 1);
      if (game.friend_outgoingNum == 2)
        s += idx == 1 ? "选上（体力）" : "选下（属性）";
      return s;
    }
  }
  return toString();
}
