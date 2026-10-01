#pragma once
#include <random>
#include <array>
#include "../GameDatabase/GameDatabase.h"
#include "Person.h"
#include "Action.h"

struct SearchParam;

struct Game;

//无人岛剧本的设施种类，0~4是速耐力根智，5是海之家
enum MujintoFacilityTypeEnum :int8_t
{
  MJ_speed = 0,
  MJ_stamina,
  MJ_power,
  MJ_guts,
  MJ_wiz,
  MJ_house,//海の家
};

//无人岛剧本的一个设施（或者建设计划里的一项）
struct MujintoFacility
{
  int8_t type;//MujintoFacilityTypeEnum
  int8_t level;//速耐力根智1~5，海之家1~3
  bool jukuren;//Lv3以上：false本能，true熟练
  MujintoFacility();
  MujintoFacility(int type, int level, bool jukuren);
  int space() const;//占用几格
  std::string toString() const;
};

struct GameSettings
{
  //显示相关
  bool playerPrint;//给人玩的时候，显示更多信息

  //参数设置

  float ptScoreRate;//每pt多少分
  float hintPtRate;//每一级hint等价多少pt
  float hintProbTimeConstant;//已经有t级技能hint时，令hint出技能的概率=0.9*(1-exp(-exp(2-t/T)))，其中T=hintProbTimeConstant，默认80
  int16_t eventStrength;//每回合有（待测）概率加这么多属性，模拟支援卡事件
  int16_t scoringMode;//评分方式

  GameSettings();
};

enum scoringModeEnum :int16_t
{
  SM_normal,//普通(凹分、评价点)模式
  SM_race,//通用大赛模式
  SM_jjc,//竞技场模式
  SM_long,//长距离模式
  SM_2400m,//2400m模式
  SM_2000m,//2000m模式
  SM_mile,//英里模式
  SM_short,//短距离模式
  SM_debug //debug模式
};

enum personIdEnum :int16_t
{
  PS_none = -1,//未分配
  PS_noncardYayoi = 6,//非卡理事长
  PS_noncardReporter = 7,//非卡记者
  PS_guest0 = 10,//无人岛嘉宾，PS_guest0+i是第i个嘉宾
  PS_guestEnd = PS_guest0 + MJ_MAX_GUEST,
};


//stage：分配人头前ST_distribute，分配人头后ST_train，训练后若需要选友人出行则ST_decideEvent，然后ST_event处理固定与随机事件，之后进入下一回合的ST_distribute
//对于神经网络ST_train可以计算policy，ST_event可以计算value
//ST_decideEvent时，列举所有选项分别进行一步（此过程没有随机性），然后分别调用神经网络计算value，取value最高的作为选项
enum StageEnum :int16_t
{
  ST_none,
  ST_distribute,//分配人头前
  ST_train,//训练前
  ST_decideEvent,//选择友人出行前
  ST_event,//处理事件前
  ST_action_randomize,//仅用于action，表示ST_train前的随机化
};

//需要处理的含选择项的事件
enum DecidingEventEnum :int16_t
{
  DecidingEvent_none,
  DecidingEvent_outing,//友人出行。idx 0普通外出，1友人出行（第3段选上），2友人出行第3段选下
};

enum TrainActionTypeEnum :int16_t
{
  T_speed = 0,
  T_stamina,
  T_power,
  T_guts,
  T_wiz,
  T_rest,
  T_outgoing, //包括合宿的“休息&外出”
  T_race, //包括生涯比赛
  T_island, //无人岛：岛训练（消耗岛训练券）
  T_none = -1, //此Action不训练，只做菜
  //TRA_redistributeCardsForTest = -2 //使用这个标记时，说明要randomDistributeCards，用于测试ai分数，在Search::searchSingleActionThread中使用
};


struct Action
{
  static const std::string trainingName[9];
  //static const Action Action_RedistributeCardsForTest;
  static const int MAX_ACTION_TYPE = 9;
  
  int16_t stage;//这个action是作用于哪个stage的，如果是ST_distribute、ST_event这些不需要做出任何选择的stage，则无视以下内容
  int16_t idx;//stage=ST_train时为训练，01234速耐力根智，5休息，6外出，7比赛，8岛训练。stage=ST_decideEvent时是选第几个
  Action();//空Action
  Action(int st);//ST_distribute、ST_event这些不需要做出任何选择的stage
  Action(int st, int idx);//需要做选择的stage

  //bool isActionStandard() const;
  //int toInt() const;
  std::string toString() const;
  std::string toString(const Game& game) const;
  //static Action intToAction(int i);
};

struct Game
{
  GameSettings gameSettings;//用户设置

  //基本状态，不包括当前回合的训练信息
  int32_t umaId;//马娘编号，见KnownUmas.cpp
  bool isLinkUma;//是否为link马
  bool isRacingTurn[TOTAL_TURN];//这回合是否比赛
  int16_t fiveStatusBonus[5];//马娘的五维属性的成长率

  int16_t turn;//回合数，从0开始，到77结束
  int16_t vital;//体力，叫做“vital”是因为游戏里就这样叫的
  int16_t maxVital;//体力上限
  int16_t motivation;//干劲，从1到5分别是绝不调到绝好调，超绝好调不在这里

  int16_t fiveStatus[5];//五维属性，1200以上不减半
  int16_t fiveStatusLimit[5];//五维属性上限，1200以上不减半
  int32_t skillPt;//技能点
  int32_t skillScore;//已买技能的分数
  int32_t hintSkillLvCount;//已经有多少级hint的技能了。hintSkillLvCount越多，hint出技能的概率越小，出属性的概率越大。
  int16_t trainLevelCount[5];//训练等级计数，每点4下加一级

  int16_t failureRateBias;//失败率改变量。练习上手=-2，练习下手=2
  bool isQieZhe;//切者 
  bool isAiJiao;//爱娇
  bool isPositiveThinking;//ポジティブ思考，友人第三段出行选上的buff，可以防一次掉心情
  bool isRefreshMind;//+5 vital every turn

  bool haveCatchedDoll;//是否抓过娃娃

  int16_t zhongMaBlueCount[5];//种马的蓝因子个数，假设只有3星
  int16_t zhongMaExtraBonus[6];//种马的剧本因子以及技能白因子（等效成pt），每次继承加多少。全大师杯因子典型值大约是30速30力200pt

  int16_t stage;//见StageEnum
  int16_t decidingEvent;//需要处理的含选择项的事件，见DecidingEventEnum
  bool isRacing;//这个回合是否在比赛

  int16_t friendship_noncard_yayoi;//非卡理事长羁绊
  int16_t friendship_noncard_reporter;//非卡记者羁绊

  Person persons[MAX_INFO_PERSON_NUM];//依次是6张卡。非卡理事长，记者，嘉宾不单独分配person类
  int16_t personDistribution[5][5];//每个训练有哪些人头id，personDistribution[哪个训练][第几个人头]，空位置为-1，0~5是6张卡，非卡理事长6，记者7，嘉宾参见personIdEnum
  //int lockedTrainingId;//是否锁训练，以及锁在了哪个训练。可以先不加，等ai做完了有时间再加。

  int16_t saihou;//赛后加成


  //剧本相关（无人岛）--------------------------------------------------------------------------------------
  //规则实现见Mujinto.cpp

  int16_t mj_linkMode;//0没带塔克布莱恩，1塔克布莱恩Lv40以下，2塔克布莱恩Lv41以上。决定每次建设计划的格数
  int8_t mj_facilityLevel[6];//已建成的设施等级，0是未建，下标见MujintoFacilityTypeEnum
  bool mj_facilityJukuren[6];//已建成的设施是否为熟练
  int8_t mj_planNum;//当前建设计划里还没建成的设施个数
  MujintoFacility mj_plan[6];//当前建设计划里还没建成的设施，按建设顺序
  int16_t mj_pioneerPt;//本期发展pt（每次评价会清零）
  int16_t mj_requiredPt1;//建成前半计划（并获得岛训练券）需要的发展pt
  int16_t mj_requiredPt2;//建成全部计划（并获得岛训练券）需要的发展pt，也是大好评需要的发展pt
  int16_t mj_ticket;//岛训练券
  int16_t mj_bonusTrainingEffect;//评价会加成：普通训练的训练效果%（上层）
  int16_t mj_bonusHint;//评价会加成：hint发生率%
  int16_t mj_bonusPioneerPt;//评价会加成：获得发展pt%
  int16_t mj_evalResult[6];//第i次评价会的结果，0未进行，1好评，2大好评（下标0不用）
  int16_t mj_guestNum;//嘉宾个数
  int8_t mj_guestType[MJ_MAX_GUEST];//嘉宾的类型（速耐力根智），岛训练才用到
  int16_t mj_deyilvBonus[6];//本回合每张卡的额外得意率（塔克布莱恩的事件与固有，上回合获得）
  int16_t mj_deyilvBonusNext[6];//下回合每张卡的额外得意率



  //单独处理剧本友人卡（塔克布莱恩），因为接近必带。其他友人团队卡的以后再考虑
  int16_t friend_type;//0没带友人卡，1 ssr卡，2 r卡
  int16_t friend_personId;//友人卡在persons里的编号
  double friend_vitalBonus;//友人卡的回复量倍数
  double friend_statusBonus;//友人卡的事件效果倍数
  int16_t friend_level;//友人卡等级（按突破数取等级上限），影响出行后获得的发展pt

  int16_t friend_stage;//0是未点击，1是已点击但未解锁出行，2是已解锁出行
  int16_t friend_outgoingNum;//友人出行走了几段（按顺序，最多5段）



  //可以通过上面的信息计算获得的非独立的信息，每回合更新一次，不需要录入
  int16_t trainValue[5][6];//训练数值的总数（下层+上层），第一个数是第几个训练，第二个数依次是速耐力根智pt
  int16_t trainVitalChange[5];//训练后的体力变化（负的体力消耗）
  int16_t failRate[5];//训练失败率
  int16_t trainHeadNum[5];//训练人头个数，不包括理事长记者
  int16_t trainShiningNum[5];//训练闪彩个数
  int16_t mj_trainPioneerPt[5];//训练成功能获得的发展pt
  int16_t mj_islandHouse[5];//岛训练时被安排到海之家的人头（没站位或者所在设施未建），-1为空
  int16_t mj_islandHouseNum;//海之家几个人
  int16_t mj_islandValue[6];//岛训练的总数（下层+上层），速耐力根智pt
  int16_t mj_islandValueLower[6];//岛训练的下层
  int16_t mj_islandPioneerPt;//岛训练获得的发展pt
  int16_t mj_islandFriendCount;//岛训练里友情训练的支援卡数
  int16_t mj_islandFriendPositions;//岛训练里发生友情训练的设施数


  //训练数值计算的中间变量，存下来方便手写逻辑进行估计
  int16_t trainValueLower[5][6];//训练数值的下层，第一个数是第几个训练，第二个数依次是速耐力根智pt体力
  //double trainValueCardMultiplier[5];//支援卡乘区=(1+总训练加成)(1+干劲系数*(1+总干劲加成))(1+0.05*总卡数)(1+友情1)(1+友情2)...

  //bool cardEffectCalculated;//支援卡效果是否已经计算过？吃无关菜不需要重新计算，分配卡组或者读json时需要置为false
  //CardTrainingEffect cardEffects[6];



  //游戏流程相关------------------------------------------------------------------------------------------

public:

  void newGame(std::mt19937_64& rand,
    GameSettings settings,
    int newUmaId,
    int umaStars,
    int newCards[6],
    int newZhongMaBlueCount[5],
    int newZhongMaExtraBonus[6]);//重置游戏，开局。umaId是马娘编号


  //这个操作是否允许且合理
  bool isLegal(Action action) const;


  std::vector<Action> getAllLegalActions() const;
  //无论什么stage，都往下进行一步，若action不合法则返回false
  void applyAction(
    std::mt19937_64& rand,
    Action action); 

  void continueUntilNextDecision(  //跳过不需要玩家选择的stage，直到下次需要选择
    std::mt19937_64& rand);

  void applyActionUntilNextDecision(
      std::mt19937_64& rand,
      Action action);




  int finalScore() const;//最终总分
  int finalScore_rank() const;//评价点
  int finalScore_sum() const;//极简大赛近似：属性之和*4+技能分
  int finalScore_mile() const;//大赛评分—英里
  bool isEnd() const;//是否已经终局




  //原则上这几个private就行，如果private在某些地方非常不方便那就改成public
  void randomizeTurn(std::mt19937_64& rand);//回合初的随机化，随机分配人头等，ST_distribute->ST_train
  void undoRandomize();//准备重新分配，ST_train->ST_distribute
  void randomDistributeHeads(std::mt19937_64& rand);//随机分配人头
  void calculateTrainingValue();//计算所有训练分别加多少，并计算失败率、训练等级提升等
  bool applyTraining(std::mt19937_64& rand, int16_t train);//ST_train->ST_decideEvent/ST_event。处理 训练/出行/比赛 本身，不包括固定事件和剧本事件。如果不合法，则返回false
  void decideEvent(std::mt19937_64& rand, int16_t idx);//友人出行选择，ST_decideEvent->ST_event

  void checkEvent(std::mt19937_64& rand);//ST_event->ST_distribute检查固定事件和随机事件，并进入下一个回合
  void checkFixedEvents(std::mt19937_64& rand);//每回合的固定事件，包括剧本事件和固定比赛和部分马娘事件等
  void checkRandomEvents(std::mt19937_64& rand);//模拟支援卡事件和随机马娘事件（随机加羁绊，体力，心情，掉心情等）

  //常用接口-----------------------------------------------------------------------------------------------

  bool loadGameFromJson(std::string jsonStr);

  //神经网络输入
  void getNNInputV1(float* buf, const SearchParam& param) const;

  void print() const;//用彩色字体显示游戏内容
  void printFinalStats() const;//显示最终结果




  //各种辅助函数与接口，可以根据需要增加或者删减-------------------------------------------------------------------------------

  inline bool isXiahesu() const //是否为夏合宿
  {
    return (turn >= 36 && turn <= 39) || (turn >= 60 && turn <= 63);
  }
  inline bool isCampTraining() const //是否用岛合宿的训练（两次夏合宿和URA期间）
  {
    return isXiahesu() || turn >= 72;
  }
  inline bool isRaceAvailable() const //是否可以额外比赛
  {
    return turn >= 13 && turn <= 71;
  }

  int calculateRealStatusGain(int value, int gain) const;//考虑1200以上为2的倍数的实际属性增加值
  void addStatus(int idx, int value);//增加属性值，并处理溢出
  void addAllStatus(int value);//同时增加五个属性值
  void addVital(int value);//增加或减少体力，并处理溢出
  void addVitalMax(int value);//增加体力上限，限制120
  void addMotivation(int value);//增加或减少心情，同时考虑“isPositiveThinking”
  void addJiBan(int idx,int value,int type);//增加羁绊，并考虑爱娇。type0是点击，type1是hint，type2是不吃任何加成的
  void addStatusFriend(int idx, int value);//友人卡事件，增加属性值或者pt（idx=5），考虑事件加成
  void addVitalFriend(int value);//友人卡事件，增加体力，考虑回复量加成
  void runRace(int basicFiveStatusBonus, int basicPtBonus);//把比赛奖励加到属性和pt上，输入是不计赛后加成的基础值
  void addTrainingLevelCount(int trainIdx, int n);//为某个训练增加n次计数
  void applyNormalTraining(std::mt19937_64& rand, int16_t train, bool success);//处理五种训练
  void addHintWithoutJiban(std::mt19937_64& rand, int idx);
  void addRandomCardHint(std::mt19937_64& rand);//随机一张支援卡的hint（评价会等）
  void jicheng(std::mt19937_64& rand);//第二三年的继承

  int getTrainingLevel(int trainIdx) const;//计算训练等级
  int calculateFailureRate(int trainType, double failRateMultiply) const;//计算训练失败率，failRateMultiply是训练失败率乘数=(1-支援卡1的失败率下降)*(1-支援卡2的失败率下降)*...

  bool isCardShining(int personIdx, int trainIdx) const;    // 判断指定卡是否闪彩
  void calculateTrainingValueSingle(int tra);//计算每个训练加多少

  //无人岛剧本（Mujinto.cpp）
  void mj_init();//开局时初始化剧本状态
  int mj_facilityIslandBonus(int status) const;//所有设施给岛训练/岛合宿的属性加成，status=0~5速耐力根智pt
  int mj_campTrainingEffect(int tra) const;//岛合宿某训练的训练效果%（上层）
  int mj_specialtyRateUp(int cardType) const;//设施给的得意率提升（区分合宿）
  int mj_positionRateUp() const;//海之家的支援卡出现率提升（不在率降低）
  int mj_hintRateUp(int tra) const;//设施与评价会的hint发生率提升%
  bool mj_alwaysHint(int tra) const;//熟练设施让岛合宿必定hint
  int mj_trainingEffectByFriend() const;//海之家：岛合宿时每个友情训练的卡+5%（下层乘算）
  int mj_calcTrainingPioneerPt(int headNum, bool shining) const;//训练获得的发展pt
  int mj_calcRacePioneerPt(bool isGoalRace) const;//比赛获得的发展pt
  bool mj_canGainPioneerPt() const;//这个回合是否能获得发展pt
  void mj_addPioneerPt(int pt);//增加发展pt，达到半数/全部时建设计划里的设施（合宿期间延后）
  void mj_buildPlan(bool all);//建成计划里的前半（达到requiredPt1）或者全部设施，并获得岛训练券
  void mj_upgradeAfterCamp();//经典年合宿结束后补建合宿期间达到条件的设施
  void mj_evaluationAndPlan(std::mt19937_64& rand, int phase);//评价会（phase>=1）与制定下一期建设计划
  void mj_makeDefaultPlan(int phase);//默认的建设计划（手写规则，第4步再做成可选择的阶段）
  void mj_addGuests(std::mt19937_64& rand, int totalCount);//PJ参加人数增加到totalCount（含自己的非友人卡）
  void mj_addDeyilvNextTurnAll(int value);//所有支援卡下回合得意率+value
  bool mj_isIslandTrainingAvailable() const;//这回合能不能岛训练
  int mj_islandTrainingEffect(int status) const;//设施给岛训练的训练效果%（上层）
  void mj_calculateIslandTraining(std::mt19937_64* rand);//安排海之家并计算岛训练数值。rand为空时不重新安排海之家
  void mj_applyIslandTraining(std::mt19937_64& rand);//进行岛训练

  //友人卡相关事件（塔克布莱恩）
  void handleFriendUnlock(std::mt19937_64& rand);//友人外出解锁
  void handleOutgoing(std::mt19937_64& rand);//外出
  void handleFriendClickEvent(std::mt19937_64& rand, int atTrain);//友人点击事件
  void handleFriendFixedEvent();//友人固定事件，新年+结算
  void runNormalOutgoing(std::mt19937_64& rand);//常规外出
  void runFriendOutgoing(std::mt19937_64& rand, bool chooseUpper);//友人外出（按顺序下一段），chooseUpper是第3段的选项


  //算分
  float getSkillScore() const;//技能分，输入神经网络之前也可能提前减去


  //显示
  void printEvents(std::string s) const;//用绿色字体显示事件
  std::string getPersonStrColored(int personId, int atTrain) const;//人物名称与羁绊等整合成带颜色的字符串，在小黑板表格中显示
};

