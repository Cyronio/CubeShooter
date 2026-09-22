/**
* The Vienna Vulkan Engine
*
* (c) bei Helmut Hlavacs, University of Vienna, 2022
*
*/

#include <algorithm>
#include <vector>
#include <cstdio>
#include <iterator>
#include <ranges>
#include <string>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <thread>

#include "VEInclude.h"
#include "stdlib.h"

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_LEFT_HANDED
#include "glm/glm.hpp"
#include "glm/gtx/matrix_operation.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/matrix_cross_product.hpp"

#include "VPE.hpp"
#include "VPEConstraintDemos.hpp"

#include "irrKlang.h"

#include "miniaudio.h"

using namespace vpe;

irrklang::ISoundEngine* SoundEngine = irrklang::createIrrKlangDevice();

double slow_motion = 3.4;

int count = 0;

bool music_started = false;
bool boss_sound = false;
bool victory_sound = false;
bool defeat_music = false;
bool lost_music = false;

bool game_start = true;

bool boss_encounter = false;
bool boss_defeated = false;
int boss_frames = 100;


int lives = 5;
bool game_lost = false;
bool game_won = false;
double total_distance = 0;

enum EMineState {
	WAIT,
	ALERT,
	ATTACK,
	DESTROY
};

struct enemyMine {
	std::shared_ptr<VPEWorld::Body> body;
	EMineState state;

	enemyMine(std::shared_ptr<VPEWorld::Body> body, EMineState state) : body(body), state(state) {}

	void setState(EMineState state) {
		this->state = state;
	}
};

struct enemyShooter {

	
	std::shared_ptr<VPEWorld::Body> body;
	EMineState state;
	int waitingFrames = 0;

	enemyShooter(std::shared_ptr<VPEWorld::Body> body, EMineState state) : body(body), state(state) {}

	void setState(EMineState state) {
		this->state = state;
	}
};

std::vector<std::shared_ptr<VPEWorld::Body>> players = {};
std::vector<std::shared_ptr<VPEWorld::Body>> obstaclesShooter = {};
std::vector<std::shared_ptr<VPEWorld::Body>> obstaclesCollided = {};
std::vector<enemyMine> enemyMines = {};
std::vector<enemyShooter> enemyShooters = {};
std::vector<std::shared_ptr<VPEWorld::Body>> skullCubes = {};
std::vector<enemyMine> enemySkullMines = {};

std::vector<std::shared_ptr<VPEWorld::Body>> projectiles = {};
std::vector<std::shared_ptr<VPEWorld::Body>> projectilesDestroyed = {};
int projectile_count = 0;

EMineState getMineState(std::shared_ptr<VPEWorld::Body> body) {
	for (auto mine : enemyMines) {
		if (mine.body->m_name == body->m_name) {
			
			return mine.state;
		}
	}
	return EMineState::WAIT;
}

EMineState getSkullMineState(std::shared_ptr<VPEWorld::Body> body) {
	for (auto mine : enemySkullMines) {
		if (mine.body->m_name == body->m_name) {

			return mine.state;
		}
	}
	return EMineState::WAIT;
}

EMineState getMineStateShooter(std::shared_ptr<VPEWorld::Body> body) {
	for (auto shooter : enemyShooters) {
		if (shooter.body->m_name == body->m_name) {

			return shooter.state;
		}
	}
	return EMineState::WAIT;
}

int getWaitingFrames(std::shared_ptr<VPEWorld::Body> body) {
	for (auto shooter : enemyShooters) {
		if (shooter.body->m_name == body->m_name) {

			return shooter.waitingFrames;
		}
	}
	return 120;
}

void setWaitingFrames(std::shared_ptr<VPEWorld::Body> body, int number) {
	for (auto &shooter : enemyShooters) {
		if (shooter.body->m_name == body->m_name) {

			shooter.waitingFrames = number;
		}
	}
}


void setMineState(std::shared_ptr<VPEWorld::Body> body, EMineState state) {
	for (auto &mine : enemyMines) {
		if (mine.body->m_name == body->m_name) {
			//std::cout << "setMineState_Names: " << mine.body->m_name << ", " << body->m_name << "\n";
			//std::cout << "Original State: " << mine.state << "\n";
			//std::cout << "Method State: " << state << "\n";
			mine.setState(state);
			//std::cout << "Set State: " << mine.state << "\n";
		}
	}
}

void setSkullMineState(std::shared_ptr<VPEWorld::Body> body, EMineState state) {
	for (auto& mine : enemySkullMines) {
		if (mine.body->m_name == body->m_name) {
			//std::cout << "setMineState_Names: " << mine.body->m_name << ", " << body->m_name << "\n";
			//std::cout << "Original State: " << mine.state << "\n";
			//std::cout << "Method State: " << state << "\n";
			mine.setState(state);
			//std::cout << "Set State: " << mine.state << "\n";
		}
	}
}

void setMineStateShooter(std::shared_ptr<VPEWorld::Body> body, EMineState state) {
	for (enemyShooter &mine : enemyShooters) {
		if (mine.body->m_name == body->m_name) {
			//std::cout << "setMineState_Names: " << mine.body->m_name << ", " << body->m_name << "\n";
			//std::cout << "Original State: " << mine.state << "\n";
			//std::cout << "Method State: " << state << "\n";
			mine.setState(state);
			//std::cout << "Set State: " << mine.state << "\n";
		}
	}
}

namespace ve {

	std::default_random_engine rnd_gen{ 12345 };					//Random numbers
	std::uniform_real_distribution<> rnd_unif{ 0.0f, 1.0f };

	VPEWorld* m_physics;

	//---------------------------------------------------------------------------------------------------------
	//callbacks for bodies

	/// <summary>
	/// This callback is used for updating the visual body whenever a physics body moves.
	/// It is also used for extrapolating the new position between two simulation slots.
	/// </summary>
	inline VPEWorld::callback_move onMoveDestroyed = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW + glmvec3(0, -5, 0);															// New position of the scene node
		glmquat orient = body->m_orientationLW;
		body->m_positionW = pos + glmvec3(0, -5, 0);
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
	};
	
	inline VPEWorld::callback_collide onCollideProjectile =
		[](std::shared_ptr<VPEWorld::Body> body1, std::shared_ptr<VPEWorld::Body> body2) {
		//std::cout << "Collision " << body1->m_name << " " << body2->m_name << "\n";
		if (std::find(obstaclesShooter.begin(), obstaclesShooter.end(), body2) != obstaclesShooter.end() && std::find(obstaclesCollided.begin(), obstaclesCollided.end(), body2) == obstaclesCollided.end()
			&& std::find(projectilesDestroyed.begin(), projectilesDestroyed.end(), body1) == projectilesDestroyed.end()) {
			//std::cout << "ProjectileCollisionTriggered \n";
			projectilesDestroyed.push_back(body1);
			obstaclesCollided.push_back(body2);
			getSceneManagerPointer()->deleteSceneNodeAndChildren(body2->m_name);
			getSceneManagerPointer()->deleteSceneNodeAndChildren(body1->m_name);
			body1->m_on_move = onMoveDestroyed;
			body2->m_on_move = onMoveDestroyed;
			SoundEngine->play2D("../../media/sounds/Shooter/explosion.wav", false);
		}
	};

	inline VPEWorld::callback_erase onErase = [&](std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* node = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		getSceneManagerPointer()->deleteSceneNodeAndChildren(
			((VESceneNode*)body->m_owner)->getName());
	};
	
	inline VPEWorld::callback_move onMovePlayer = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		body->m_linear_velocityW = glmvec3(0,0,1) * 6.00_real;
		total_distance += 1*dt;
	};

	inline VPEWorld::callback_move onMove = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
	};

	inline VPEWorld::callback_move onMoveJupiter = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		body->setForce(0ul, VPEWorld::Force{ {0, -24.79_real, 0} });
	};

	inline VPEWorld::callback_move onMoveSkull = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));

		glmvec3 playerPos = players[0]->m_positionW;
		float distance = length(playerPos - pos);

		if (distance < 20) {
			boss_encounter = true;
			body->m_linear_velocityW = glmvec3(0, 0, 1) * 5.00_real;
			if (boss_sound == false) {
				SoundEngine->removeAllSoundSources();
				SoundEngine->play2D("../../media/sounds/Shooter/crisis.mp3", false);
				boss_sound = true;
			}
			
		}

		if (distance < 8) {
			body->m_on_move = onMoveJupiter;
			boss_defeated = true;
			if (defeat_music == false) {
				SoundEngine->play2D("../../media/sounds/Shooter/boss_defeat.wav", false);
				defeat_music = true;
			}
			
		}
		
	};

	inline VPEWorld::callback_move onMoveMine = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		glmvec3 playerPos = players[0]->m_positionW;
		float distance = length(playerPos - pos);
		//std::cout << "Distance Mine / Player: " << std::to_string(distance) << "\n";
		if (getMineState(body) == EMineState::WAIT) {
			//std::cout << "State: WAIT \n";
			if (distance < 20) {
				setMineState(body, EMineState::ATTACK);
			}
		}
		if (getMineState(body) == EMineState::ATTACK) {
			//std::cout << "State: ATTACK \n";
			glmvec3 direction = playerPos - pos;
			body->m_linear_velocityW = normalize(direction) * 4.50_real;
		}

	};

	inline VPEWorld::callback_move onMoveSkullMine = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		glmvec3 playerPos = players[0]->m_positionW;
		float distance = length(playerPos - pos);
		//std::cout << "Distance Mine / Player: " << std::to_string(distance) << "\n";
		if (getSkullMineState(body) == EMineState::WAIT) {
			//std::cout << "State: WAIT \n";
			//std::cout << "skull mine name: " << enemySkullMines[0].body->m_name << ", body name: " << body->m_name << "\n";
			if (distance < 19 && enemySkullMines[0].body->m_name == body->m_name) {
				
				setSkullMineState(body, EMineState::ATTACK);
				SoundEngine->play2D("../../media/sounds/Shooter/weird.wav", false);
			}
		}
		if (getSkullMineState(body) == EMineState::WAIT) {
			//std::cout << "State: WAIT \n";
			if (distance < 15 && enemySkullMines[1].body->m_name == body->m_name) {
				setSkullMineState(body, EMineState::ATTACK);
				SoundEngine->play2D("../../media/sounds/Shooter/weird.wav", false);
			}
		}
		if (getSkullMineState(body) == EMineState::WAIT) {
			//std::cout << "State: WAIT \n";
			if (distance < 10 && enemySkullMines[2].body->m_name == body->m_name) {
				setSkullMineState(body, EMineState::ATTACK);
				SoundEngine->play2D("../../media/sounds/Shooter/weird.wav", false);
			}
		}
		//std::cout << "EnemyMine State: " << getSkullMineState(body) << "\n";
		if (getSkullMineState(body) == EMineState::ATTACK) {
			//std::cout << "State: ATTACK \n";
			glmvec3 direction = playerPos - pos;
			body->m_linear_velocityW = normalize(direction) * 4.50_real;
		}
		if (boss_encounter && getSkullMineState(body) == EMineState::WAIT) {
			body->m_linear_velocityW = glmvec3(0, 0, 1) * 5.00_real;
		}
		

	};

	inline VPEWorld::callback_move onMoveShooter = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		glmvec3 playerPos = players[0]->m_positionW;
		float distance = length(playerPos - pos);
		//std::cout << "Distance Shooter / Player: " << std::to_string(distance) << "\n";
		if (getMineStateShooter(body) == EMineState::WAIT) {
			//std::cout << "State: WAIT \n";
			if (distance < 20) {
				setMineStateShooter(body, EMineState::ATTACK);
				//std::cout << "State after Distance_Trigger: " << getMineState(body) << "\n";;
			}
		}
		else if (getMineStateShooter(body) == EMineState::ATTACK) {

			int waitingFrames = getWaitingFrames(body);
			
			//std::cout << "State: ATTACK \n";
			//std::cout << "Waiting Frames: " << waitingFrames << "\n";
			if (getWaitingFrames(body) == 0) {
				VESceneNode* obstacle;
				glmvec3 direction = playerPos - pos;
				//std::cout << "DIrection: " << direction << "\n";
				VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube.obj", 0, getRoot()));
				//std::cout << "SceneManagaer-Check\n";
				auto cubey = std::make_shared<VPEWorld::Body>(m_physics, "Cube" + std::to_string(count), obstacle, &m_physics->g_cube, glmvec3(0.1f), glmvec3(0.0f, 0.0f, -2.0f), glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, 0,0); //m_physics, "Cube" + std::to_string(count), obstacle, &m_physics->g_cube, glmvec3(0.1f), glmvec3(2.0f,2.0f,35.0f), glmquat(1.0f, 0, 0, 0), direction /* normalize(direction) * 2.5_real*/, glmvec3(0.0f), 0.1_real, m_physics->m_restitution, m_physics->m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
				//std::cout << "CUbeCreated-Check\n";
				cubey->m_on_move = onMove;
				cubey->m_on_erase = onErase;
				m_physics->addBody(cubey);
				//std::cout << "CUbey registered\n";
				obstaclesShooter.push_back(cubey);
				count++;
				setWaitingFrames(body, 100);
			}
			else {
				setWaitingFrames(body, getWaitingFrames(body) - 1);
			}
		}
	};

	inline VPEWorld::callback_move onMoveProjectile = [&](double dt, std::shared_ptr<VPEWorld::Body> body) {
		VESceneNode* cube = static_cast<VESceneNode*>(body->m_owner);								// Owner is a pointer to a scene node
		glmvec3 pos = body->m_positionW;															// New position of the scene node
		glmquat orient = body->m_orientationLW;														// New orientation of the scende node
		body->stepPosition(dt, pos, orient, false);													// Extrapolate
		cube->setTransform(VPEWorld::Body::computeModel(pos, orient, body->m_scale));
		body->m_linear_velocityW = glmvec3(0, 0, 1) * 20.0_real;
		glmvec3 playerPos = players[0]->m_positionW;
		float distance = length(playerPos - pos);
		if (distance > 10) {
			for (auto projectile : projectiles) {
				if (projectile->m_name == body->m_name) {
					projectilesDestroyed.push_back(body);
				}
			}
			getSceneManagerPointer()->deleteSceneNodeAndChildren(cube->getName());

			body->m_on_move = onMoveDestroyed;
		}
	};

	inline VPEWorld::callback_collide onCollideObstacle =
		[](std::shared_ptr<VPEWorld::Body> body1, std::shared_ptr<VPEWorld::Body> body2) {
		//std::cout << "Collision " << body1->m_name << " " << body2->m_name << "\n";
		if (std::find(obstaclesShooter.begin(), obstaclesShooter.end(), body2) != obstaclesShooter.end() && std::find(obstaclesCollided.begin(), obstaclesCollided.end(), body2) == obstaclesCollided.end()) {
			lives--;
			SoundEngine->play2D("../../media/sounds/Shooter/hit_new.wav", false);
			obstaclesCollided.push_back(body2);
		
		}
	};


	//----------------------------------------------------------------------------------------------
	//Listener for driving the simulation 

	class VEEventListenerPhysics : public VEEventListener {

		std::default_random_engine rnd_gen{ 12345 };					//Random numbers
		std::uniform_real_distribution<> rnd_unif{ 0.0f, 1.0f };		//Random numbers
	protected:

		/// <summary>
		/// This drives the simulation!!!
		/// </summary>
		void onFrameStarted(veEvent event) {

			//if (game_start) return;

			if (game_lost == false && game_start == false && game_won == false) {
				m_physics->tick(event.dt);

				if (music_started == false) {
					SoundEngine->play2D("../../media/sounds/Shooter/space.mp3", false);
					music_started = true;
				}

				glm::vec4 translate = glm::vec4(0.0, 0.0, 0.0, 1.0); //total translation
				glm::vec4 rot4 = glm::vec4(1.0); //total rotation around the axes, is 4d !
				float angle = 0.0;
				float rotSpeed = 2.0;

				VECamera* pCamera = getSceneManagerPointer()->getCamera();
				VESceneNode* pParent = pCamera->getParent();

				translate = pCamera->getTransform() * glm::vec4(0.0, 0.0, 1.0, 1.0); //forward

				float speed = 6.0f;
				glm::vec3 trans = speed * glm::vec3(translate.x, translate.y, translate.z);
				pParent->multiplyTransform(glm::translate(glm::mat4(1.0f), (float)event.dt * trans));
			}
			else if (game_lost == true && slow_motion > 0) {
				m_physics->tick(event.dt / 10.0);
				slow_motion = slow_motion - 1*event.dt;
				std::cout << "Slow motion:" << slow_motion << std::endl;
			}

			else if (game_lost == true && slow_motion <= 0) {
				SoundEngine->removeAllSoundSources();
			}

			else if (game_won == true && slow_motion > 0) {
				if (victory_sound == false) {
					SoundEngine->removeAllSoundSources();
					SoundEngine->play2D("../../media/sounds/Shooter/victory.mp3", false);
					victory_sound = true;
				}
				m_physics->tick(event.dt);
				slow_motion = slow_motion - 1 * event.dt;
				std::cout << "Slow motion:" << slow_motion << std::endl;
			}
			

			if (lives <= 0) {
				lives = 0;
				game_lost = true;
				if (lost_music == false) {
					SoundEngine->removeAllSoundSources();
					SoundEngine->play2D("../../media/sounds/Shooter/boss_defeat.mp3", false); //originally homage to SNES game
					lost_music = true;
				}
				
			}

		}

		VPEWorld* m_physics;																		//Pointer to the physics world

	public:
		///Constructor of class EventListenerCollision
		VEEventListenerPhysics(std::string name, VPEWorld* physics)
			: VEEventListener(name),m_physics{ physics } { };

		///Destructor of class EventListenerCollision
		virtual ~VEEventListenerPhysics() {};
	};

	//----------------------------------------------------------------------------------------------
	//Listener for creating bodies with keyboard

	/// <summary>
	/// This is a callback that is called in each loop. It implements a simple rigid body 
	/// physics engine.
	/// </summary>
	class VEEventListenerPhysicsKeys : public VEEventListener {

		std::default_random_engine rnd_gen{ 12345 };					//Random numbers
		std::uniform_real_distribution<> rnd_unif{ 0.0f, 1.0f };		//Random numbers

	public:

		/// <summary>
		/// Callback for event key stroke. Depending on the key pressed, bodies are created.
		/// </summary>
		/// <param name="event"> The keyboard event. </param>
		/// <returns> False, so the key is not consumed. </returns>
		bool onKeyboard(veEvent event) {
			if (game_lost == false && game_won == false) {

				if (event.idata1 == GLFW_KEY_F) {//}&& event.idata3 == GLFW_PRESS) {
					std::shared_ptr<VPEWorld::Body> body = players[0];
					glmvec3 pos = body->m_positionW;
					if (!(pos.x < -3.8))
						body->m_positionW = glmvec3(pos.x - 5.0f * event.dt, pos.y, pos.z);
					if (boss_encounter) {
						for (auto cube : skullCubes) {
							glmvec3 cubePos = cube->m_positionW;
							if (!(pos.x < -3.8) && !boss_defeated)
								cube->m_positionW = glmvec3(cubePos.x - 5.0f * event.dt, cubePos.y, cubePos.z);
						}
						for (auto mine : enemySkullMines) {
							glmvec3 cubePos = mine.body->m_positionW;
							if (!(pos.x < -3.8) && !boss_defeated)
								mine.body->m_positionW = glmvec3(cubePos.x - 5.0f * event.dt, cubePos.y, cubePos.z);
						}
					}

				}

				if (event.idata1 == GLFW_KEY_H) { //}&& event.idata3 == GLFW_PRESS) {
					std::shared_ptr<VPEWorld::Body> body = players[0];								// Owner is a pointer to a scene node
					glmvec3 pos = body->m_positionW;
					if (!(pos.x > 3.8))
						body->m_positionW = glmvec3(pos.x + 5.0f * event.dt, pos.y, pos.z);
					if (boss_encounter) {
						for (auto cube : skullCubes) {
							glmvec3 cubePos = cube->m_positionW;
							if (!(pos.x > 3.8) && !boss_defeated)
								cube->m_positionW = glmvec3(cubePos.x + 5.0f * event.dt, cubePos.y, cubePos.z);
						}
			
						for (auto mine : enemySkullMines) {
							glmvec3 cubePos = mine.body->m_positionW;
							if (!(pos.x > 3.8) && !boss_defeated)
								mine.body->m_positionW = glmvec3(cubePos.x + 5.0f * event.dt, cubePos.y, cubePos.z);
						}
					}
				}

				if (event.idata1 == GLFW_KEY_T) { //}&& event.idata3 == GLFW_PRESS) {
					std::shared_ptr<VPEWorld::Body> body = players[0];								// Owner is a pointer to a scene node
					glmvec3 pos = body->m_positionW;
					if (!(pos.y > 2.8))// New position of the scene node
						body->m_positionW = glmvec3(pos.x, pos.y + 5.0f * event.dt, pos.z);
				}

				if (event.idata1 == GLFW_KEY_G) { //}&& event.idata3 == GLFW_PRESS) {
					std::shared_ptr<VPEWorld::Body> body = players[0];								// Owner is a pointer to a scene node
					glmvec3 pos = body->m_positionW;
					if (!(pos.y < 0.6))// New position of the scene node
						body->m_positionW = glmvec3(pos.x, pos.y - 5.0f * event.dt, pos.z);
				}

				if (event.idata1 == GLFW_KEY_L && event.idata3 == GLFW_PRESS) {
					//std::cout << "L pressed \n";
					std::shared_ptr<VPEWorld::Body> body = players[0];								// Owner is a pointer to a scene node
					glmvec3 pos = body->m_positionW;
					VESceneNode* projectile;
					VECHECKPOINTER(projectile = getSceneManagerPointer()->loadModel("Projectile" + std::to_string(projectile_count), "../../media/models/test/crate0", "cube_yellow.obj", 0, getRoot()));

					auto cube = std::make_shared<VPEWorld::Body>(m_physics, "Projectile" + std::to_string(projectile_count), projectile, &m_physics->g_cube, glmvec3(0.1f), pos + glmvec3(0.0f, 0.0f, 1.0f), glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics->m_restitution, m_physics->m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
					cube->m_on_move = onMoveProjectile;
					cube->m_on_erase = onErase;
					m_physics->addBody(cube);
					m_physics->addCollider(cube, onCollideProjectile);
					//std::cout << "Projectiles-Size: " << projectiles.size() << "\n";
					projectiles.push_back(cube);
					//std::cout << "Projectiles-Size after push_back: " << projectiles.size() << "\n";
					projectile_count++;
					//std::cout << "Projectile Name: " << cube->m_name << "\n";
					SoundEngine->play2D("../../media/sounds/Shooter/laser.wav", false);
				}
			}
			return false;
		};

		VPEWorld* m_physics;	//Pointer to the physics world

	public:
		/// Constructor of class EventListenerCollision
		VEEventListenerPhysicsKeys(std::string name, VPEWorld* physics)
			: VEEventListener(name), m_physics{physics} { };

		///Destructor of class EventListenerCollision
		virtual ~VEEventListenerPhysicsKeys() {};
	};

	class VEEventListenerGUI : public VEEventListener {
	protected:

		virtual void onDrawOverlay(veEvent event) {
			VESubrender_Nuklear* pSubrender = (VESubrender_Nuklear*)getEnginePointer()->getRenderer()->getOverlay();
			if (pSubrender == nullptr) return;

			struct nk_context* ctx = pSubrender->getContext();

			if (total_distance > 290) {
				game_won = true;
			}

			if (game_start) {
				if (nk_begin(ctx, "", nk_rect(850, 400, 220, 120), NK_WINDOW_BORDER)) {
					nk_layout_row_dynamic(ctx, 45, 1);
					nk_label(ctx, "CUBE SHOOTER", NK_TEXT_CENTERED);
					if (nk_button_label(ctx, "Start")) {
						game_start = false;
						//return;
					}
				}
				nk_end(ctx);
			}



			else if (game_won) {
				if (nk_begin(ctx, "", nk_rect(500, 500, 200, 120), NK_WINDOW_BORDER)) {
					nk_layout_row_dynamic(ctx, 45, 1);
					nk_label(ctx, "You Win!", NK_TEXT_CENTERED);
					if (nk_button_label(ctx, "Exit")) {
						exit(0);
					}
				}
				nk_end(ctx);
			}

			else if (!game_lost) {
					if (nk_begin(ctx, "", nk_rect(0, 0, 250, 120), NK_WINDOW_BORDER)) {
						char outbuffer[100];
						nk_layout_row_dynamic(ctx, 45, 1);
						std::string energy = "";
						for (int i = 0; i < lives; i++) energy = energy + "* ";
						std::string livesString = "Energy: " + energy;
						sprintf(outbuffer, livesString.c_str());
						nk_label(ctx, outbuffer, NK_TEXT_LEFT);

						nk_layout_row_dynamic(ctx, 45, 1);
						std::string distanceString = "Distance: " + std::to_string(round(total_distance));
						sprintf(outbuffer, distanceString.c_str());
						nk_label(ctx, outbuffer, NK_TEXT_LEFT);
					}
					nk_end(ctx);
			}
			
			else {
				if (nk_begin(ctx, "", nk_rect(500, 500, 200, 120), NK_WINDOW_BORDER)) {
					nk_layout_row_dynamic(ctx, 45, 1);
					nk_label(ctx, "Game Over", NK_TEXT_CENTERED);
					if (nk_button_label(ctx, "Exit")) {
						exit(0);
					}
				}
				nk_end(ctx);

			};

			//nk_end(ctx);
		}

	public:
		///Constructor of class EventListenerGUI
		VEEventListenerGUI(std::string name) : VEEventListener(name) { };

		///Destructor of class EventListenerGUI
		virtual ~VEEventListenerGUI() {};
	};
	
	//----------------------------------------------------------------------------------------------
	// My custom engine

	/// User defined manager class, derived from VEEngine
	class MyVulkanEngine : public VEEngine {
	public:

		VPEWorld m_physics;
		VEEventListenerPhysics*	m_physics_listener;
		VEEventListenerPhysicsKeys* m_physics_listener_keys;
		VEEventListenerGUI* m_event_listener_gui;

		MyVulkanEngine(veRendererType type = veRendererType::VE_RENDERER_TYPE_FORWARD,
			bool debug = false) : VEEngine(type, debug) {};

		/// Register an event listener to interact with the user
		virtual void registerEventListeners() {
			VEEngine::registerEventListeners();

			registerEventListener(m_physics_listener = new VEEventListenerPhysics(
				"Physics", &m_physics), { veEvent::VE_EVENT_FRAME_STARTED });
			registerEventListener(m_physics_listener_keys = new VEEventListenerPhysicsKeys(
				"Physics Keys", &m_physics), { veEvent::VE_EVENT_KEYBOARD });
			registerEventListener(m_event_listener_gui = new VEEventListenerGUI(
				"Game GUI"), { veEvent::VE_EVENT_DRAW_OVERLAY });
		};

		virtual void createTree(int x, int z) {
			VESceneNode* obstacle;

			//int count = 0;

			for (int i = 0; i < 3; i++) {
				VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_dark.obj", 0, getRoot()));

				auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)i, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
				cube->m_on_move = onMove;
				cube->m_on_erase = onErase;
				m_physics.addBody(cube);


				obstaclesShooter.push_back(cube);
				count++;
				
			}
		}

		virtual void createEnemyMine(int x, int y, int z) {
			VESceneNode* obstacle;

			VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_red.obj", 0, getRoot()));

			auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)y, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
			cube->m_on_move = onMoveMine;
			cube->m_on_erase = onErase;
			m_physics.addBody(cube);


			enemyMines.push_back(enemyMine(cube, EMineState::WAIT));
			obstaclesShooter.push_back(cube);
			count++;

			
		}

		virtual void createEnemyShooter(int x, int y, int z) {
			VESceneNode* obstacle;

			VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_red.obj", 0, getRoot()));

			auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)y, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
			cube->m_on_move = onMoveShooter;
			cube->m_on_erase = onErase;
			m_physics.addBody(cube);


			enemyShooters.push_back(enemyShooter(cube, EMineState::WAIT));
			obstaclesShooter.push_back(cube);
			count++;


		}

		void createMoonBouncer(int x, int y, int z) {
			VESceneNode* obstacle;

			VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_bright.obj", 0, getRoot()));

			auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)y, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f,0.0f,-4.0f), glmvec3(0.0f), 0.5_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
			cube->setForce(0ul, VPEWorld::Force{ {0, -1.62_real, 0} });
			cube->setForce(12, VPEWorld::Force{ {0, 0, -3.0} });
			cube->m_on_move = onMove;
			cube->m_on_erase = onErase;
			m_physics.addBody(cube);


			obstaclesShooter.push_back(cube);
			count++;
		}

		void createCube(int x, int y, int z) {
			VESceneNode* obstacle;
			VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_dark.obj", 0, getRoot()));

			auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)y, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
			cube->m_on_move = onMoveSkull;
			cube->m_on_erase = onErase;
			m_physics.addBody(cube);


			obstaclesShooter.push_back(cube);
			skullCubes.push_back(cube);
			count++;
		}

		void createSkullMine(int x, int y, int z) {
			VESceneNode* obstacle;

			VECHECKPOINTER(obstacle = getSceneManagerPointer()->loadModel("Cube" + std::to_string(count), "../../media/models/test/crate0", "cube_red.obj", 0, getRoot()));

			auto cube = std::make_shared<VPEWorld::Body>(&m_physics, "Cube" + std::to_string(count), obstacle, &m_physics.g_cube, glmvec3(0.8f), glmvec3{ (float)x, (float)y, (float)z }, glmquat(1.0f, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0.1_real, m_physics.m_restitution, m_physics.m_friction); // glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1);
			cube->m_on_move = onMoveSkullMine;
			cube->m_on_erase = onErase;
			m_physics.addBody(cube);


			enemySkullMines.push_back(enemyMine(cube, EMineState::WAIT));
			obstaclesShooter.push_back(cube);
			count++;
		}
		
		void createSkull(int z) {
			for (int i = 0; i < 3; i++){
				createCube(i - 1, 6, z);
				}
			for (int i = 0; i < 5; i++) {
				createCube(i - 2, 5, z);
			}
			createCube(-2, 4, z); createCube(0, 4, z); createCube(2, 4, z);
			for (int i = 0; i < 3; i++) {
				createCube(i - 1, 3, z);
			}
			createCube(-1, 2, z); createCube(1, 2, z);
			for (int i = 0; i < 3; i++) {
				createCube(i - 1, 1, z);
			}
			createSkullMine(-1, 4, z);
			createSkullMine(1, 4, z);
			createSkullMine(0, 2, z);

		}

		/// Load the first level into the game engine
		/// The engine uses Y-UP, Left-handed
		virtual void loadLevel(uint32_t numLevel = 1) {

			std::default_random_engine rnd_gen{ 12345 };					//Random numbers
			std::uniform_real_distribution<> rnd_unif{ 0.0f, 1.0f };		//Random numbers

			VEEngine::loadLevel(numLevel);
			
			// Create standard cameras and lights

			VESceneNode* pScene;																	// Get Root Node
			VECHECKPOINTER(pScene =
				getSceneManagerPointer()->createSceneNode("Level 1", getRoot()));

			// Scene models

			VESceneNode* sp1;
			VECHECKPOINTER(sp1 =
				getSceneManagerPointer()->createSkybox("The Sky", "../../media/models/test/sky/cloudy",
					{ "0001.png" /*"bluecloud_ft.jpg", "Sun.jpg" */, "bluecloud_bk.jpg", "bluecloud_up.jpg", "bluecloud_dn.jpg",
					"bluecloud_rt.jpg", "bluecloud_lf.jpg" }, pScene));

			VESceneNode* e4;
			VECHECKPOINTER(e4 = getSceneManagerPointer()->loadModel(
				"The Plane", "../../media/models/test/plane", "plane_t_n_s.obj", 0, pScene));
			e4->setTransform(glm::scale(glm::translate(glm::vec3{ 0,0,0, }),
				glm::vec3(1000.0f, 1.0f, 1000.0f)));

			VEEntity* pE4;
			VECHECKPOINTER(pE4 = (VEEntity*)getSceneManagerPointer()->getSceneNode(
				"The Plane/plane_t_n_s.obj/plane/Entity_0"));
			pE4->setParam(glm::vec4(1000.0f, 1000.0f, 0.0f, 0.0f));

			getSceneManagerPointer()->getSceneNode("StandardCameraParent")->setPosition({ 0,1,-4 }); //former z = -4		
			
			createTree(-3, 20);
			createTree(0, 40);
			createTree(2, 60);
			createTree(-2, 80);
			createTree(2, 80);
			createTree(1, 100);
			createTree(0, 100);
			createTree(-1, 100);
			createTree(4, 110);
			createTree(-4, 130);
			createTree(-3, 130);
			createTree(-2, 130);
			createTree(-1, 130);
			createTree(0, 130);
			createTree(1, 130);
			createTree(4, 150);
			createTree(3, 150);
			createTree(2, 150);
			createTree(1, 150);
			createTree(0, 150);
			createTree(-1, 150);
			createTree(-2, 310);
			createTree(-1, 310);
			createTree(0, 310);
			createTree(1, 310);
			createTree(2, 310);

			createEnemyMine(1, 1, 200);
			createEnemyMine(-4, 2, 250);
			createEnemyMine(4, 2, 250);
			createEnemyMine(-1, 1, 280);
			createEnemyMine(3, 3, 290);
			createEnemyMine(-4, 2, 300);

			createMoonBouncer(-2, 5, 80);
			createMoonBouncer(4, 7, 100);
			createMoonBouncer(-3, 5, 140);
			createMoonBouncer(3, 9, 150);

			createSkull(350);


			VESceneNode* player;

			VECHECKPOINTER(player = getSceneManagerPointer()->loadModel("Player1", "../../media/models/test/crate0", "cube_blue.obj", 0, getRoot()));

			glmvec3 positionCamera{ getSceneManagerPointer()->getSceneNode("StandardCameraParent")->getWorldTransform()[3] };
			glmvec3 dir{ getSceneManagerPointer()->getSceneNode("StandardCamera")->getWorldTransform()[2] };

			auto cube1 = std::make_shared<VPEWorld::Body>(&m_physics, "Player1", player, &m_physics.g_cube, glmvec3(0.9, 0.9, 0.9), (positionCamera + 6.0_real * dir) /* + glmvec3(0, -1, 0)*/, glmquat(1, 0, 0, 0), glmvec3(0.0f), glmvec3(0.0f), 0, 0, 1); // 0.1_real, 0.1_real, 1.0_real);
			cube1->m_on_move = onMovePlayer;
			cube1->m_on_erase = onErase;
			m_physics.addBody(cube1);
			cube1->setForce(0ul, VPEWorld::Force{ {0, 0, 0} });
			m_physics.addCollider(cube1, onCollideObstacle);

			players.push_back(cube1);

		}
	};
};



//--------------------------------------------------------------------------------------------------

using namespace ve;




int main() {

	bool debug = false;
	MyVulkanEngine mve(veRendererType::VE_RENDERER_TYPE_FORWARD, debug);							// Enable or disable debugging (callback, validation layers)
	mve.initEngine();
	mve.loadLevel(1);
	mve.run();
	
	return 0;
}
