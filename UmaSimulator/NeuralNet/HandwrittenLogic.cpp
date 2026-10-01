#include <cassert>
#include <iostream>
#include "Evaluator.h"
#include "../Search/Search.h"


const double statusWeights[5] = { 6,6,6,6,6 };
const double jibanValue = 3;
const double vitalFactorStart = 3;
const double vitalFactorEnd = 10;
const double vitalScaleTraining = 1.0;

const double reserveStatusFactor = 50;//控属性时给每回合预留多少，从0逐渐增加到这个数字

const double smallFailValue = -300;
const double bigFailValue = -800;
const double outgoingBonusIfNotFullMotivationStart = 100;//掉心情时提高外出分数
const double outgoingBonusIfNotFullMotivationEnd = 800;//掉心情时提高外出分数
const double raceBonus = 0;//比赛收益，不考虑体力

const double mj_pioneerPtValue = 2.0;//无人岛：大好评之前每点发展pt的估值（第4步再细调）

//一个分段函数，用来控属性
inline double statusSoftFunction(double x, double reserve, double reserveInvX2)//reserve是控属性保留空间（降低权重），reserveInvX2是1/(2*reserve)
{
  if (x >= 0)return 0;
  if (x > -reserve)return -x * x * reserveInvX2;
  return x + 0.5 * reserve;
}

static void statusGainEvaluation(const Game& g, double* result,int remainTrainingTurns) { //result依次是五种训练的估值
  int remainTurn = remainTrainingTurns;//这次训练后还有几个训练回合
  //ura期间的一个回合视为两个回合，因此不需要额外处理
  //if (remainTurn == 2)remainTurn = 1;//ura第二回合
  //else if (remainTurn >= 4)remainTurn -= 2;//ura第一回合

  double reserve = reserveStatusFactor * remainTurn * (1 - double(remainTurn) / (TOTAL_TURN * 2));
  double reserveInvX2 = 1 / (2 * reserve);

  double finalBonus0 = 170;
  //finalBonus0 += 30;//ura3和最终事件
  //if (remainTurn >= 1)finalBonus0 += 20;//ura2
  //if (remainTurn >= 2)finalBonus0 += 20;//ura1

  double remain[5]; //每种属性还有多少空间

  for (int i = 0; i < 5; i++)
  {
    remain[i] = g.fiveStatusLimit[i] - g.fiveStatus[i] - finalBonus0;
  }

  //if (g.friend_type != 0)
  //{
  //  remain[0] -= 25;
  //  remain[4] -= 25;
  //}


  for (int tra = 0; tra < 5; tra++)
  {
    double res = 0;
    for (int sta = 0; sta < 5; sta++)
    {
      double s0 = statusSoftFunction(-remain[sta], reserve, reserveInvX2);
      double s1 = statusSoftFunction(g.trainValue[tra][sta] - remain[sta], reserve, reserveInvX2);
      res += statusWeights[sta] * (s1 - s0);
    }
    res += g.gameSettings.ptScoreRate * g.trainValue[tra][5];
    result[tra] = res;
  }




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
static double pioneerPtEvaluation(const Game& game, int pioneerPt)
{
  if (!game.mj_canGainPioneerPt())
    return 0;
  int need = game.mj_requiredPt2 - game.mj_pioneerPt;
  if (need <= 0)
    return 0;
  return mj_pioneerPtValue * std::min(need, pioneerPt);
}

double getRestOutingEvaluation(const Game& game, Action& bestAction, double vitalFactor, int maxVitalEquvalant, double vitalEvalBeforeTrain, int remainTrainingTurns)
{
  double motivationValue = 0;
  if (game.motivation < 5)
    motivationValue = outgoingBonusIfNotFullMotivationStart + (game.turn / double(TOTAL_TURN)) * (outgoingBonusIfNotFullMotivationEnd - outgoingBonusIfNotFullMotivationStart);

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
    outingValue += 150;//属性与得意率
    outingValue += pioneerPtEvaluation(game, 30 + (game.friend_level - 20) * 50 / 30);
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

Action Evaluator::handWrittenStrategy(const Game& game)
{
  auto allActions = game.getAllLegalActions();
  if (allActions.size() == 1)
    return allActions[0];
  if (allActions.size() == 0)
    return Action();

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

    double vitalFactor = vitalFactorStart + (game.turn / double(TOTAL_TURN)) * (vitalFactorEnd - vitalFactorStart);

    int maxVitalEquvalant = calculateMaxVitalEquvalant(game, remainTrainingTurns);
    double vitalEvalBeforeTrain = vitalEvaluation(std::min(maxVitalEquvalant, int(game.vital)), game.maxVital);

    //外出/休息
    Action restAction;
    double restValue = getRestOutingEvaluation(game, restAction, vitalFactor, maxVitalEquvalant, vitalEvalBeforeTrain, remainTrainingTurns);
    if (restValue > bestValue)
    {
      bestValue = restValue;
      bestAction = restAction;
    }

    //比赛
    if (game.isRaceAvailable())
    {
      double value = raceBonus;

      int vitalAfterRace = std::min(maxVitalEquvalant, -15 + game.vital);
      value += vitalFactor * (vitalEvaluation(vitalAfterRace, game.maxVital) - vitalEvalBeforeTrain);
      value += pioneerPtEvaluation(game, game.mj_calcRacePioneerPt(false));

      if (PrintHandwrittenLogicValueForDebug)
        std::cout << " " << value << std::endl;
      if (value > bestValue)
      {
        bestValue = value;
        bestAction.idx = T_race;
      }
    }

    //训练
    {
      double statusGainE[5];
      statusGainEvaluation(game, statusGainE, remainTrainingTurns);

      for (int tra = 0; tra < 5; tra++)
      {
        double value = statusGainE[tra];
        value += pioneerPtEvaluation(game, game.mj_trainPioneerPt[tra]);

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
              value += 150;
            else if (game.friend_stage == FriendStage_beforeUnlockOutgoing)
              value += 60;
            else
              value += 40;
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

              value += jibanAdd * jibanValue;
            }

            if (p.isHint)
            {
              double hintBonus = p.cardParam.hintLevel == 0 ?
                (1.6 * (statusWeights[0] + statusWeights[1] + statusWeights[2] + statusWeights[3] + statusWeights[4])) :
                game.gameSettings.hintPtRate * game.gameSettings.ptScoreRate * p.cardParam.hintLevel;
              value += hintBonus * hintProb;
            }
          }
        }

        int vitalAfterTrain = std::min(maxVitalEquvalant, game.trainVitalChange[tra] + game.vital);
        value += vitalScaleTraining * vitalFactor * (vitalEvaluation(vitalAfterTrain, game.maxVital) - vitalEvalBeforeTrain);

        //到目前为止都是训练成功的value
        double failRate = game.failRate[tra];
        if (failRate > 0)
        {
          double bigFailProb = failRate;
          if (failRate < 20)bigFailProb = 0;
          double failValueAvg = 0.01 * bigFailProb * bigFailValue + (1 - 0.01 * bigFailProb) * smallFailValue;

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
