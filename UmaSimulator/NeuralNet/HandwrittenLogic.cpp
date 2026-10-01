#include <cstdlib>
#include <cassert>
#include <iostream>
#include "Evaluator.h"
#include "../Search/Search.h"


#include <fstream>
#include "../External/json.hpp"

HandwrittenParams handwrittenParams;

const char* const HandwrittenParams::names[HandwrittenParams::NUM] = {
  "statusWeight","jibanValue","vitalFactorStart","vitalFactorEnd","vitalScaleTraining","reserveStatusFactor",
  "smallFailValue","bigFailValue","outgoingBonusStart","outgoingBonusEnd","raceBonus",
  "friendFirstClickValue","friendBeforeUnlockValue","friendAfterUnlockValue","friendOutingValue",
  "mj_pioneerPtValue","mj_keepTicketValue","mj_ticketNearThreshold",
  "mj_planHouseLv2Value","mj_planHouseLv3Value","mj_planSpeedExtra","mj_planJukurenFactor",
  "mj_planBaseWeight","mj_planCardWeight","mj_planLevelFactor"
};
double HandwrittenParams::* const HandwrittenParams::members[HandwrittenParams::NUM] = {
  &HandwrittenParams::statusWeight,&HandwrittenParams::jibanValue,&HandwrittenParams::vitalFactorStart,
  &HandwrittenParams::vitalFactorEnd,&HandwrittenParams::vitalScaleTraining,&HandwrittenParams::reserveStatusFactor,
  &HandwrittenParams::smallFailValue,&HandwrittenParams::bigFailValue,&HandwrittenParams::outgoingBonusStart,
  &HandwrittenParams::outgoingBonusEnd,&HandwrittenParams::raceBonus,
  &HandwrittenParams::friendFirstClickValue,&HandwrittenParams::friendBeforeUnlockValue,&HandwrittenParams::friendAfterUnlockValue,&HandwrittenParams::friendOutingValue,
  &HandwrittenParams::mj_pioneerPtValue,&HandwrittenParams::mj_keepTicketValue,&HandwrittenParams::mj_ticketNearThreshold,
  &HandwrittenParams::mj_planHouseLv2Value,&HandwrittenParams::mj_planHouseLv3Value,&HandwrittenParams::mj_planSpeedExtra,
  &HandwrittenParams::mj_planJukurenFactor,&HandwrittenParams::mj_planBaseWeight,&HandwrittenParams::mj_planCardWeight,
  &HandwrittenParams::mj_planLevelFactor
};

bool HandwrittenParams::loadJson(const std::string& path)
{
  std::ifstream f(path);
  if (!f.good())return false;
  nlohmann::json j;
  f >> j;
  for (int i = 0; i < NUM; i++)
    if (j.contains(names[i]))
      this->*members[i] = j[names[i]].get<double>();
  return true;
}

void HandwrittenParams::saveJson(const std::string& path) const
{
  nlohmann::json j;
  for (int i = 0; i < NUM; i++)
    j[names[i]] = this->*members[i];
  std::ofstream f(path);
  f << j.dump(2) << std::endl;
}

//一个分段函数，用来控属性
inline double statusSoftFunction(double x, double reserve, double reserveInvX2)//reserve是控属性保留空间（降低权重），reserveInvX2是1/(2*reserve)
{
  if (x >= 0)return 0;
  if (x > -reserve)return -x * x * reserveInvX2;
  return x + 0.5 * reserve;
}

//某次训练增加gain（速耐力根智pt）的估值，考虑控属性
static double statusGainEvaluationSingle(const HandwrittenParams& P, const Game& g, const int16_t* gain, int remainTrainingTurns)
{
  int remainTurn = remainTrainingTurns;//这次训练后还有几个训练回合
  double reserve = P.reserveStatusFactor * remainTurn * (1 - double(remainTurn) / (TOTAL_TURN * 2));
  double reserveInvX2 = 1 / (2 * reserve);

  double finalBonus0 = 170;//结算时还会加的属性

  double res = 0;
  for (int sta = 0; sta < 5; sta++)
  {
    double remain = g.fiveStatusLimit[sta] - g.fiveStatus[sta] - finalBonus0;
    double s0 = statusSoftFunction(-remain, reserve, reserveInvX2);
    double s1 = statusSoftFunction(gain[sta] - remain, reserve, reserveInvX2);
    res += P.statusWeight * (s1 - s0);
  }
  res += g.gameSettings.ptScoreRate * gain[5];
  return res;
}

static void statusGainEvaluation(const HandwrittenParams& P, const Game& g, double* result, int remainTrainingTurns) { //result依次是五种训练的估值
  for (int tra = 0; tra < 5; tra++)
    result[tra] = statusGainEvaluationSingle(P, g, g.trainValue[tra], remainTrainingTurns);
}

//还有几个训练回合（不含当前回合）
static int countRemainTrainingTurns(const Game& g)
{
  int nonRaceTurn = 0;
  for (int i = TOTAL_TURN - 1; i > g.turn; i--)
  {
    if (!g.isRacingTurn[i])nonRaceTurn++;
  }
  return nonRaceTurn;

}


static double calculateMaxVitalEquvalant(const Game& g, int remainTrainingTurns)
{
  if (remainTrainingTurns <= 0)
    return 0;//最后一回合
  int maxVitalEq = 28 + 25 * remainTrainingTurns;
  if (maxVitalEq > g.maxVital)
    maxVitalEq = g.maxVital;
  return maxVitalEq;

}

static double vitalEvaluation(int vital, int maxVital)
{
  if (vital <= 50)
    return 2.0 * vital;
  else if (vital <= 70)
    return 1.5 * (vital - 50) + vitalEvaluation(50, maxVital);
  else if (vital <= maxVital)
    return 1.0 * (vital - 70) + vitalEvaluation(70, maxVital);
  else
    return vitalEvaluation(maxVital, maxVital);
}

//无人岛：获得pioneerPt点发展pt的估值。只有达到大好评（requiredPt2）之前的部分有价值
static double pioneerPtEvaluation(const HandwrittenParams& P, const Game& game, int pioneerPt)
{
  if (!game.mj_canGainPioneerPt())
    return 0;
  int need = game.mj_requiredPt2 - game.mj_pioneerPt;
  if (need <= 0)
    return 0;
  return P.mj_pioneerPtValue * std::min(need, pioneerPt);
}

//留着岛训练券的估值：快拿到新券（会溢出）或者剩下能用的回合不多时就是0
static double keepTicketEvaluation(const HandwrittenParams& P, const Game& game)
{
  int usableTurns = 0;//以后还能岛训练的回合数
  //经典年合宿结束时多半会补建并把券数设为1，手里的券要在合宿前用掉
  int horizon = game.turn < 36 ? 36 : 72;
  for (int t = game.turn + 1; t < horizon; t++)
  {
    bool camp = (t >= 36 && t <= 39) || (t >= 60 && t <= 63);
    if (!camp && !game.isRacingTurn[t])
      usableTurns++;
  }
  if (game.mj_ticket > usableTurns)
    return 0;
  if (game.turn < 60)
  {
    int next = game.mj_pioneerPt < game.mj_requiredPt1 ? game.mj_requiredPt1 :
      game.mj_pioneerPt < game.mj_requiredPt2 ? game.mj_requiredPt2 : -1;
    if (next >= 0 && next - game.mj_pioneerPt <= P.mj_ticketNearThreshold)
      return 0;
  }
  return P.mj_keepTicketValue;
}

static double islandTrainingEvaluation(const HandwrittenParams& P, const Game& game, int remainTrainingTurns)
{
  double value = statusGainEvaluationSingle(P, game, game.mj_islandValue, remainTrainingTurns);
  value += pioneerPtEvaluation(P, game, game.mj_islandPioneerPt);
  //全部支援卡羁绊+10
  for (int p = 0; p < 6; p++)
  {
    const Person& ps = game.persons[p];
    if (ps.personType == PersonType_card && ps.friendship < 80)
      value += std::min(10, 80 - ps.friendship) * P.jibanValue;
  }
  value -= keepTicketEvaluation(P, game);
  return value;
}

double getRestOutingEvaluation(const HandwrittenParams& P, const Game& game, Action& bestAction, double vitalFactor, int maxVitalEquvalant, double vitalEvalBeforeTrain, int remainTrainingTurns)
{
  double motivationValue = 0;
  if (game.motivation < 5)
    motivationValue = P.outgoingBonusStart + (game.turn / double(TOTAL_TURN)) * (P.outgoingBonusEnd - P.outgoingBonusStart);

  if (game.isXiahesu())
  {
    Action action(ST_train, T_outgoing);
    int vitalGain = 40;
    int vitalAfterRest = std::min(maxVitalEquvalant, vitalGain + game.vital);
    double value = vitalFactor * (vitalEvaluation(vitalAfterRest, game.maxVital) - vitalEvalBeforeTrain);
    value += motivationValue;
    bestAction = action;
    return value;
  }

  int vitalGain = 50;
  int vitalAfterRest = std::min(maxVitalEquvalant, vitalGain + game.vital);
  double value = vitalFactor * (vitalEvaluation(vitalAfterRest, game.maxVital) - vitalEvalBeforeTrain);
  Action action(ST_train, T_rest);
  if (PrintHandwrittenLogicValueForDebug)
    std::cout << action.toString() << " " << value << std::endl;

  double bestValue = value;
  bestAction = action;

  action.idx = T_outgoing;
  double outingValue = motivationValue;

  //友人出行：体力+心情+属性+发展pt，比休息好
  bool isFriendOutgoingAvailable =
    game.friend_type != 0 &&
    game.friend_stage == FriendStage_afterUnlockOutgoing &&
    game.friend_outgoingNum < 5;
  if (isFriendOutgoingAvailable)
  {
    int friendVitalGain = game.friend_outgoingNum == 2 ? 50 : 25;
    friendVitalGain = int(friendVitalGain * game.friend_vitalBonus);
    int vitalAfterOuting = std::min(maxVitalEquvalant, friendVitalGain + game.vital);
    outingValue += vitalFactor * (vitalEvaluation(vitalAfterOuting, game.maxVital) - vitalEvalBeforeTrain);
    outingValue += P.friendOutingValue;//属性与得意率
    outingValue += pioneerPtEvaluation(P, game, 30 + (game.friend_level - 20) * 50 / 30);
  }

  if (PrintHandwrittenLogicValueForDebug)
    std::cout << action.toString() << " " << outingValue << std::endl;
  if (outingValue > bestValue)
  {
    bestValue = outingValue;
    bestAction = action;
  }
  return bestValue;
}

//无人岛：建设计划里加一个设施的估值。海之家Lv1最优先，其次按卡组里各类型支援卡的张数升级，本能优于熟练
static double planCandidateEvaluation(const HandwrittenParams& P, const Game& game, int idx)
{
  MujintoFacility f = game.mj_planCandidate(idx);
  if (f.type == MJ_house)
    return f.level == 1 ? 1000 : f.level == 2 ? P.mj_planHouseLv2Value : P.mj_planHouseLv3Value;
  double weight = P.mj_planBaseWeight;
  for (int i = 0; i < 6; i++)
  {
    const Person& p = game.persons[i];
    if (p.personType == PersonType_card && p.cardParam.cardType == f.type)
      weight += P.mj_planCardWeight;
  }
  if (f.type == MJ_speed)
    weight += P.mj_planSpeedExtra;
  double value = weight * (1 + P.mj_planLevelFactor * f.level);
  //测试用：环境变量 UMAAI_JK_MASK 按位强制Lv3以上选熟练（第0~4位对应速耐力根智），其余选本能
  static int jkMask = getenv("UMAAI_JK_MASK") ? atoi(getenv("UMAAI_JK_MASK")) : -1;
  if (jkMask >= 0 && f.level >= 3)
  {
    bool want = (jkMask >> f.type) & 1;
    if (f.jukuren != want)return -1e8;
    return value;
  }
  if (f.jukuren)
    value *= P.mj_planJukurenFactor;
  return value;
}

Action Evaluator::handWrittenStrategy(const Game& game)
{
  return handWrittenStrategy(game, handwrittenParams);
}

Action Evaluator::handWrittenStrategy(const Game& game, const HandwrittenParams& P)
{
  auto allActions = game.getAllLegalActions();
  if (allActions.size() == 1)
    return allActions[0];
  if (allActions.size() == 0)
    return Action();

  if (game.stage == ST_plan)
  {
    Action best = allActions[0];
    double bestValue = -1e9;
    for (const Action& a : allActions)
    {
      double v = planCandidateEvaluation(P, game, a.idx);
      if (v > bestValue)
      {
        bestValue = v;
        best = a;
      }
    }
    return best;
  }
  if (game.stage == ST_decideEvent)
  {
    if (game.decidingEvent == DecidingEvent_outing)
    {
      //体力快满时可能只是想加心情，用普通出行留着友人出行
      if (game.turn < 66 && (game.maxVital - game.vital <= 15) && game.friend_outgoingNum < 5)
        return Action(ST_decideEvent, 0);
      if (game.friend_outgoingNum == 2 && game.maxVital - game.vital < 30)
        return Action(ST_decideEvent, 2);//第3段，体力不缺就选属性
      return Action(ST_decideEvent, 1);
    }
    else throw "handWrittenStrategy未知decidingEvent";
  }
  else if (game.stage == ST_train)
  {
    Action bestAction;
    bestAction.stage = ST_train;
    bestAction.idx = -1;

    if (game.isEnd())return bestAction;
    //比赛
    if (game.isRacing)
    {
      bestAction.idx = T_race;
      return bestAction;
    }

    int remainTrainingTurns = countRemainTrainingTurns(game);

    double bestValue = -1e4;

    double vitalFactor = P.vitalFactorStart + (game.turn / double(TOTAL_TURN)) * (P.vitalFactorEnd - P.vitalFactorStart);

    int maxVitalEquvalant = calculateMaxVitalEquvalant(game, remainTrainingTurns);
    double vitalEvalBeforeTrain = vitalEvaluation(std::min(maxVitalEquvalant, int(game.vital)), game.maxVital);

    //外出/休息
    Action restAction;
    double restValue = getRestOutingEvaluation(P, game, restAction, vitalFactor, maxVitalEquvalant, vitalEvalBeforeTrain, remainTrainingTurns);
    if (restValue > bestValue)
    {
      bestValue = restValue;
      bestAction = restAction;
    }

    //比赛
    if (game.isRaceAvailable())
    {
      double value = P.raceBonus;

      int vitalAfterRace = std::min(maxVitalEquvalant, -15 + game.vital);
      value += vitalFactor * (vitalEvaluation(vitalAfterRace, game.maxVital) - vitalEvalBeforeTrain);
      value += pioneerPtEvaluation(P, game, game.mj_calcRacePioneerPt(false));

      if (PrintHandwrittenLogicValueForDebug)
        std::cout << " " << value << std::endl;
      if (value > bestValue)
      {
        bestValue = value;
        bestAction.idx = T_race;
      }
    }

    //岛训练（不耗体力，不会失败）
    if (game.mj_isIslandTrainingAvailable())
    {
      double value = islandTrainingEvaluation(P, game, remainTrainingTurns);
      if (PrintHandwrittenLogicValueForDebug)
        std::cout << "岛训练 " << value << std::endl;
      if (value > bestValue)
      {
        bestValue = value;
        bestAction.idx = T_island;
      }
    }

    //训练
    {
      double statusGainE[5];
      statusGainEvaluation(P, game, statusGainE, remainTrainingTurns);

      for (int tra = 0; tra < 5; tra++)
      {
        double value = statusGainE[tra];
        value += pioneerPtEvaluation(P, game, game.mj_trainPioneerPt[tra]);

        //处理hint和羁绊
        int cardHintNum = 0;//所有hint随机取一个，所以打分的时候取平均
        for (int j = 0; j < 5; j++)
        {
          int p = game.personDistribution[tra][j];
          if (p < 0)break;//没人
          if (p >= 6)continue;//不是卡
          if (game.persons[p].isHint)
            cardHintNum += 1;
        }
        double hintProb = cardHintNum > 0 ? 1.0 / cardHintNum : 0.0;
        for (int j = 0; j < 5; j++)
        {
          int pi = game.personDistribution[tra][j];
          if (pi < 0)break;//没人
          if (pi >= 6)continue;//不是卡
          const Person& p = game.persons[pi];
          if (p.personType == PersonType_scenarioCard)//友人卡
          {
            if (game.friend_stage == FriendStage_notClicked)
              value += P.friendFirstClickValue;
            else if (game.friend_stage == FriendStage_beforeUnlockOutgoing)
              value += P.friendBeforeUnlockValue;
            else
              value += P.friendAfterUnlockValue;
          }
          else if (p.personType == PersonType_card)
          {
            if (p.friendship < 80)
            {
              double jibanAdd = 7;
              if (game.isAiJiao)jibanAdd += 2;
              if (p.isHint)
              {
                jibanAdd += 5 * hintProb;
                if (game.isAiJiao)jibanAdd += 2 * hintProb;
              }
              jibanAdd = std::min(double(80 - p.friendship), jibanAdd);

              value += jibanAdd * P.jibanValue;
            }

            if (p.isHint)
            {
              double hintBonus = p.cardParam.hintLevel == 0 ?
                (1.6 * 5 * P.statusWeight) :
                game.gameSettings.hintPtRate * game.gameSettings.ptScoreRate * p.cardParam.hintLevel;
              value += hintBonus * hintProb;
            }
          }
        }

        int vitalAfterTrain = std::min(maxVitalEquvalant, game.trainVitalChange[tra] + game.vital);
        value += P.vitalScaleTraining * vitalFactor * (vitalEvaluation(vitalAfterTrain, game.maxVital) - vitalEvalBeforeTrain);

        //到目前为止都是训练成功的value
        double failRate = game.failRate[tra];
        if (failRate > 0)
        {
          double bigFailProb = failRate;
          if (failRate < 20)bigFailProb = 0;
          double failValueAvg = 0.01 * bigFailProb * P.bigFailValue + (1 - 0.01 * bigFailProb) * P.smallFailValue;

          value = 0.01 * failRate * failValueAvg + (1 - 0.01 * failRate) * value;
        }

        Action action(ST_train,tra);

        if (value > bestValue)
        {
          bestValue = value;
          bestAction = action;
        }
        if (PrintHandwrittenLogicValueForDebug)
          std::cout << action.toString() << " " << value << std::endl;
      }
    }
    return bestAction;
  }
  else throw "未知stage";
  return Action();
}
