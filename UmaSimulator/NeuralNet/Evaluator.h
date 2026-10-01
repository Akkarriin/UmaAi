#pragma once
#include <vector>
#include <string>
#include "NNInput.h"
#include "Model.h"
#include "../Game/Game.h"

struct SearchParam;

//手写策略的可调参数（自动调参程序 UmaAiTuneHandwritten 会搜索这些值）
struct HandwrittenParams
{
  double statusWeight = 6;//每点属性的估值
  double jibanValue = 3;//每点羁绊（80以下）的估值
  double vitalFactorStart = 3;//体力估值系数，开局
  double vitalFactorEnd = 10;//体力估值系数，结束
  double vitalScaleTraining = 1.0;//训练时体力变化的权重
  double reserveStatusFactor = 50;//控属性时给每回合预留多少
  double smallFailValue = -300;//训练小失败的估值
  double bigFailValue = -800;//训练大失败的估值
  double outgoingBonusStart = 100;//掉心情时外出的加分，开局
  double outgoingBonusEnd = 800;//掉心情时外出的加分，结束
  double raceBonus = 0;//比赛收益，不考虑体力
  double friendFirstClickValue = 150;//第一次点塔克
  double friendBeforeUnlockValue = 60;//点塔克（未解锁出行）
  double friendAfterUnlockValue = 40;//点塔克（已解锁出行）
  double friendOutingValue = 150;//塔克出行的属性与得意率
  double mj_pioneerPtValue = 2.0;//大好评之前每点发展pt的估值
  double mj_keepTicketValue = 1000;//留着岛训练券以后用的估值
  double mj_ticketNearThreshold = 150;//离下一次建设不到这么多发展pt时不留券
  double mj_planHouseLv2Value = 3;//计划里海之家Lv2的估值（海之家Lv1总是最优先）
  double mj_planHouseLv3Value = 4;//计划里海之家Lv3的估值
  double mj_planSpeedExtra = 1.0;//速度设施额外的权重
  double mj_planJukurenFactor = 0.6;//熟练相对本能的估值
  double mj_planBaseWeight = 1;//卡组里没有的类型的权重
  double mj_planCardWeight = 2;//卡组里每张该类型支援卡增加的权重
  double mj_planLevelFactor = 0.15;//等级越高越优先

  static const int NUM = 25;
  static const char* const names[NUM];
  static double HandwrittenParams::* const members[NUM];
  bool loadJson(const std::string& path);//只覆盖文件里有的字段，文件不存在返回false
  void saveJson(const std::string& path) const;
};
extern HandwrittenParams handwrittenParams;//handWrittenStrategy默认用的参数
//每个线程一个evaluator
//所有线程共用一个model
class Evaluator
{
public:
  Model* model;
  //static lock;//所有的evaluator共用一个lock
  int maxBatchsize;

  std::vector<Game> gameInput;

  std::vector<float> inputBuf;
  std::vector<float> outputBuf;

#if USE_BACKEND == BACKEND_CUDA
  //把输入向量转化为稀疏形式，以节省pcie带宽
  std::vector<uint16_t> inputBufOnesIdx;
  std::vector<uint16_t> inputBufFloatIdx;
  std::vector<float> inputBufFloatValue;
#endif

  std::vector<ModelOutputValueV1> valueResults;
  //std::vector<ModelOutputPolicyV1> policyResults;
  std::vector<Action> actionResults;


  //void evaluate(const Game* games, const float* targetScores, int mode, int gameNum);//计算games中gameNum局游戏的输出。如果没有model，就使用手写逻辑计算policy，但不能计算非结束状态的value
  void evaluateSelf(int mode, const SearchParam& param);//计算gameInput的输出。如果没有model，就使用手写逻辑计算policy，但不能计算非结束状态的value
  
  Evaluator();
  Evaluator(Model* model, int maxBatchsize);

  static Action handWrittenStrategy(const Game& game);
  static Action handWrittenStrategy(const Game& game, const HandwrittenParams& P);
  static ModelOutputValueV1 extractValueFromNNOutputBuf(float* buf);//神经网络计算完之后，把神经网络的输出转化成ModelOutputValueV1和Action
  static Action extractActionFromNNOutputBuf(float* buf, const Game& game);//神经网络计算完之后，把神经网络的输出转化成ModelOutputValueV1和Action
};