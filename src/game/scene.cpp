// This file is part of Dust Racing 2D.
// Copyright (C) 2011 Jussi Lind <jussi.lind@iki.fi>
//
// Dust Racing 2D is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// Dust Racing 2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Dust Racing 2D. If not, see <http://www.gnu.org/licenses/>.

#include "scene.hpp"

#include "ai.hpp"
#include "car.hpp"
#include "checkeredflag.hpp"
#include "credits.hpp"
#include "game.hpp"
#include "inputhandler.hpp"
#include "intro.hpp"
#include "help.hpp"
#include "layers.hpp"
#include "mainmenu.hpp"
#include "messageoverlay.hpp"
#include "particlemanager.hpp"
#include "race.hpp"
#include "renderer.hpp"
#include "settings.hpp"
#include "settingsmenu.hpp"
#include "startlights.hpp"
#include "startlightsoverlay.hpp"
#include "statemachine.hpp"
#include "timingoverlay.hpp"
#include "track.hpp"
#include "trackdata.hpp"
#include "trackobject.hpp"
#include "trackselectionmenu.hpp"
#include "tracktile.hpp"
#include "treeview.hpp"

#include "../common/config.hpp"
#include "../common/targetnodebase.hpp"

#include <MenuManager>

#include <MCAssetManager>
#include <MCCamera>
#include <MCFrictionGenerator>
#include <MCGLAmbientLight>
#include <MCGLDiffuseLight>
#include <MCGLScene>
#include <MCGLShaderProgram>
#include <MCLogger>
#include <MCObject>
#include <MCSurface>
#include <MCSurfaceView>
#include <MCTextureFont>
#include <MCTypes>
#include <MCWorld>

#include <QObject>
#include <QApplication>

#include <algorithm>
#include <cassert>

// Default scene size.
int Scene::m_width  = 1024;
int Scene::m_height = 768;

static const MCFloat METERS_PER_PIXEL = 0.05f;

Scene::Scene(Game & game, StateMachine & stateMachine, Renderer & renderer)
: m_game(game)
, m_stateMachine(stateMachine)
, m_renderer(renderer)
, m_messageOverlay(new MessageOverlay)
, m_race(game, NUM_CARS, *m_messageOverlay)
, m_activeTrack(nullptr)
, m_world(new MCWorld)
, m_startlights(new Startlights(*m_messageOverlay))
, m_startlightsOverlay(new StartlightsOverlay(*m_startlights))
, m_checkeredFlag(new CheckeredFlag)
, m_trackSelectionMenu(nullptr)
, m_mainMenu(nullptr)
, m_help(nullptr)
, m_credits(nullptr)
, m_settings(nullptr)
, m_menuManager(nullptr)
, m_intro(new Intro)
, m_particleManager(new ParticleManager)
{
    QObject::connect(m_startlights, SIGNAL(raceStarted()), &m_race, SLOT(start()));

    m_stateMachine.setRenderer(renderer);
    m_stateMachine.setRace(m_race);
    m_stateMachine.setStartlights(*m_startlights);
    m_stateMachine.setIntro(*m_intro);

    m_cameraOffset[0] = 0.0;
    m_cameraOffset[1] = 0.0;

    m_checkeredFlag->setDimensions(width(), height());
    m_intro->setDimensions(width(), height());
    m_startlightsOverlay->setDimensions(width(), height());
    m_messageOverlay->setDimensions(width(), height());

    m_timingOverlay[0].setTiming(m_race.timing());
    m_timingOverlay[0].setRace(m_race);
    m_timingOverlay[1].setTiming(m_race.timing());
    m_timingOverlay[1].setRace(m_race);

    m_world->enableDepthTestOnLayer(Layers::Tree, true);
    m_world->enableDepthTestOnLayer(Layers::Meshes, true);
    m_world->setMetersPerPixel(METERS_PER_PIXEL);

    MCAssetManager::textureFontManager().font("default").surface().setShaderProgram(
        &Renderer::instance().program("text"));
    MCAssetManager::textureFontManager().font("default").surface().setShadowShaderProgram(
        &Renderer::instance().program("textShadow"));

    const MCGLAmbientLight ambientLight(1.0, 0.9, 0.95, 0.5);
    const MCGLDiffuseLight diffuseLight(MCVector3dF(1.0, -1.0, -0.25), 1.0, 0.9, 0.85, 0.75);

    Renderer::instance().program("car").setDiffuseLight(diffuseLight);
    Renderer::instance().program("car").setAmbientLight(ambientLight);
    Renderer::instance().program("master").setDiffuseLight(diffuseLight);
    Renderer::instance().program("master").setAmbientLight(ambientLight);
    Renderer::instance().program("particle").setDiffuseLight(diffuseLight);
    Renderer::instance().program("particle").setAmbientLight(ambientLight);
    Renderer::instance().program("tile2d").setDiffuseLight(diffuseLight);
    Renderer::instance().program("tile2d").setAmbientLight(ambientLight);
    Renderer::instance().program("tile3d").setDiffuseLight(diffuseLight);
    Renderer::instance().program("tile3d").setAmbientLight(ambientLight);

    createMenus();
}

void Scene::createCars()
{
    const int   humanPower = 200000; // This in Watts
    const float humanDrag  = 5.0;

    m_race.removeCars();
    m_cars.clear();
    m_ai.clear();

    // Create and add cars.
    for (int i = 0; i < NUM_CARS; i++)
    {
        Car::Description desc;

        Car * car = nullptr;
        if (i == 0 || (i == 1 && m_game.hasTwoHumanPlayers()))
        {
            desc.power         = humanPower;
            desc.dragQuadratic = humanDrag;

            const std::string image = i ? "carGrey" : "carPink";
            car = new Car(desc, MCAssetManager::surfaceManager().surface(image), i, true);
        }
        else if (m_game.hasComputerPlayers())
        {
            // Introduce some variance to the power of computer players so that the
            // slowest cars have less power than the human player and the fastest
            // cars have more power than the human player.
            desc.power                = humanPower / 2 + (i + 1) * humanPower / NUM_CARS;
            desc.accelerationFriction = 0.5 + 0.3 * float(i + 1) / NUM_CARS;
            desc.dragQuadratic        = humanDrag;
            desc.turningImpulse       = 0.3;
            desc.slideFriction        = 1.0;
            desc.brakingFriction      = 2.0;

            if (i == NUM_CARS - 1)
            {
                car = new Car(desc, MCAssetManager::surfaceManager().surface("carBlack"), i, false);
            }
            else if (i == NUM_CARS - 2)
            {
                car = new Car(desc, MCAssetManager::surfaceManager().surface("carOrange"), i, false);
            }
            else
            {
                car = new Car(desc, MCAssetManager::surfaceManager().surface("carYellow"), i, false);
            }

            m_ai.push_back(AIPtr(new AI(*car)));
        }

        if (car)
        {
            car->setLayer(Layers::Cars);
            car->view()->setShaderProgram(&Renderer::instance().program("car"));
            car->view()->setShadowShaderProgram(&Renderer::instance().program("masterShadow"));

            m_cars.push_back(CarPtr(car));
            m_race.addCar(*car);
        }
    }

    if (m_game.hasTwoHumanPlayers())
    {
        m_timingOverlay[1].setCarToFollow(*m_cars.at(1));
    }

    m_timingOverlay[0].setCarToFollow(*m_cars.at(0));
}

int Scene::width()
{
    return Scene::m_width;
}

int Scene::height()
{
    return Scene::m_height;
}

void Scene::setSize(int width, int height)
{
    Scene::m_width  = width;
    Scene::m_height = height;
}

void Scene::createMenus()
{
    m_menuManager = new MTFH::MenuManager;

    m_mainMenu = new MainMenu("main", width(), height());
    QObject::connect(
        static_cast<MainMenu *>(m_mainMenu), SIGNAL(exitGameRequested()), &m_game, SLOT(exitGame()));
    m_menuManager->addMenu(*m_mainMenu);

    m_help = new Help("help", width(), height());
    m_menuManager->addMenu(*m_help);

    m_credits = new Credits("credits", width(), height());
    m_menuManager->addMenu(*m_credits);

    m_settings = new SettingsMenu("settings", width(), height());
    m_menuManager->addMenu(*m_settings);

    m_trackSelectionMenu = new TrackSelectionMenu("trackSelection",
        width(), height(), *this, m_stateMachine);
    m_menuManager->addMenu(*m_trackSelectionMenu);

    m_menuManager->enterMenu(*m_mainMenu);
}

void Scene::updateFrame(InputHandler & handler, float timeStep)
{
    if (m_stateMachine.state() == StateMachine::GameTransitionIn  ||
        m_stateMachine.state() == StateMachine::GameTransitionOut ||
        m_stateMachine.state() == StateMachine::DoStartlights     ||
        m_stateMachine.state() == StateMachine::Play)
    {
        if (m_activeTrack)
        {
            if (m_race.started())
            {
                processUserInput(handler);
                updateAI();
            }

            updateWorld(timeStep);
            updateRace();

            if (m_game.hasTwoHumanPlayers())
            {
                updateCameraLocation(m_camera[0], m_cameraOffset[0], *m_cars.at(0));
                updateCameraLocation(m_camera[1], m_cameraOffset[1], *m_cars.at(1));
            }
            else
            {
                updateCameraLocation(m_camera[0], m_cameraOffset[0], *m_cars.at(0));
            }
        }
    }
}

void Scene::updateAnimations()
{
    if (m_game.hasTwoHumanPlayers())
    {
        m_timingOverlay[1].update();
    }

    m_timingOverlay[0].update();

    m_messageOverlay->update();

    for (CarPtr car : m_cars)
    {
        car->update();
    }

    for (TreeView * view : m_treeViews)
    {
        view->update();
    }
}

void Scene::updateWorld(float timeStep)
{
    // Step time
    m_world->stepTime(timeStep);
}

void Scene::updateRace()
{
    // Update race situation
    m_race.update();
}

void Scene::updateCameraLocation(MCCamera & camera, MCFloat & offset, MCObject & object)
{
    // Update camera location with respect to the car speed.
    // Make changes a bit smoother so that an abrupt decrease
    // in the speed won't look bad.
    MCVector2dF loc(object.location());

    const float offsetAmplification = m_game.hasTwoHumanPlayers() ? 9.6 : 13.8;
    const float smooth              = 0.2;

    offset += (object.velocity().lengthFast() - offset) * smooth;
    loc    += object.direction() * offset * offsetAmplification;

    camera.setPos(loc.i(), loc.j());
}

void Scene::processUserInput(InputHandler & handler)
{
    bool steering = false;

    for (int i = 0; i < (m_game.hasTwoHumanPlayers() ? 2 : 1); i++)
    {
        m_cars.at(i)->clearStatuses();

        // Handle accelerating / braking
        if (handler.getActionState(i, InputHandler::IA_DOWN))
        {
            if (!m_race.timing().raceCompleted(i))
            {
                m_cars.at(i)->brake();
            }
        }
        else if (handler.getActionState(i, InputHandler::IA_UP))
        {
            if (!m_race.timing().raceCompleted(i))
            {
                m_cars.at(i)->accelerate();
            }
        }

        // Handle turning
        if (handler.getActionState(i, InputHandler::IA_LEFT))
        {
            m_cars.at(i)->turnLeft();
            steering = true;
        }
        else if (handler.getActionState(i, InputHandler::IA_RIGHT))
        {
            m_cars.at(i)->turnRight();
            steering = true;
        }

        if (!steering)
        {
            m_cars.at(i)->noSteering();
        }
    }
}

void Scene::updateAI()
{
    for (AIPtr ai : m_ai)
    {
        const bool isRaceCompleted = m_race.timing().raceCompleted(ai->car().index());
        ai->update(isRaceCompleted);
    }
}

void Scene::setActiveTrack(Track & activeTrack)
{
    m_activeTrack = &activeTrack;
    m_stateMachine.setTrack(*m_activeTrack);

    m_world->removeParticleVisibilityCameras();
    if (m_game.hasTwoHumanPlayers())
    {
        m_camera[0].init(
            Scene::width() / 2, Scene::height(), 0, 0, activeTrack.width(), activeTrack.height());
        m_world->addParticleVisibilityCamera(m_camera[0]);

        m_camera[1].init(
            Scene::width() / 2, Scene::height(), 0, 0, activeTrack.width(), activeTrack.height());
        m_world->addParticleVisibilityCamera(m_camera[1]);
    }
    else
    {
        m_camera[0].init(
            Scene::width(), Scene::height(), 0, 0, activeTrack.width(), activeTrack.height());
        m_world->addParticleVisibilityCamera(m_camera[0]);
    }

    // Remove previous objects;
    m_world->clear();

    setWorldDimensions();

    createCars();

    resizeOverlays();

    addCarsToWorld();

    addTrackObjectsToWorld();

    initRace();

    for (AIPtr ai : m_ai)
    {
        ai->setTrack(activeTrack);
    }
}

void Scene::setWorldDimensions()
{
    assert(m_world);
    assert(m_activeTrack);

    // Update world dimensions according to the
    // active track.
    const MCUint minX = 0;
    const MCUint maxX = m_activeTrack->width();
    const MCUint minY = 0;
    const MCUint maxY = m_activeTrack->height();
    const MCUint minZ = 0;
    const MCUint maxZ = 1000;

    m_world->setDimensions(minX, maxX, minY, maxY, minZ, maxZ, METERS_PER_PIXEL);
}

void Scene::addCarsToWorld()
{
    // Add objects to the world
    for (CarPtr car : m_cars)
    {
        car->addToWorld();
    }
}

void Scene::addTrackObjectsToWorld()
{
    assert(m_activeTrack);

    const unsigned int trackObjectCount =
        m_activeTrack->trackData().objects().count();

    m_treeViews.clear();

    for (unsigned int i = 0; i < trackObjectCount; i++)
    {
        TrackObject & trackObject = static_cast<TrackObject &>(
            m_activeTrack->trackData().objects().object(i));
        MCObject & mcObject = trackObject.object();
        mcObject.addToWorld();
        mcObject.translate(mcObject.initialLocation());
        mcObject.rotate(mcObject.initialAngle());

        if (TreeView * treeView = dynamic_cast<TreeView *>(mcObject.view()))
        {
            m_treeViews.push_back(treeView);
        }
    }
}

void Scene::resizeOverlays()
{
    if (m_game.hasTwoHumanPlayers())
    {
        m_timingOverlay[0].setDimensions(width() / 2, height());
        m_timingOverlay[1].setDimensions(width() / 2, height());
    }
    else
    {
        m_timingOverlay[0].setDimensions(width(), height());
    }
}

void Scene::initRace()
{
    assert(m_activeTrack);
    m_race.init(*m_activeTrack, m_game.lapCount());
}

Track & Scene::activeTrack() const
{
    assert(m_activeTrack);
    return *m_activeTrack;
}

MCWorld & Scene::world() const
{
    assert(m_world);
    return *m_world;
}

TrackSelectionMenu & Scene::trackSelectionMenu() const
{
    assert(m_trackSelectionMenu);
    return *m_trackSelectionMenu;
}

void Scene::render()
{
    const MCFloat fadeValue = Renderer::instance().fadeValue();

    switch (m_stateMachine.state())
    {
    case StateMachine::DoIntro:

        m_renderer.glScene().setSplitType(MCGLScene::Single);
        m_intro->setFadeValue(fadeValue);
        m_intro->render();

        break;

    case StateMachine::Menu:
    case StateMachine::MenuTransitionOut:
    case StateMachine::MenuTransitionIn:

        if (m_stateMachine.isFading())
        {
            Renderer::instance().program("menu").setFadeValue(fadeValue);
            Renderer::instance().program("tile2d").setFadeValue(fadeValue);
            Renderer::instance().program("tile3d").setFadeValue(fadeValue);
            Renderer::instance().program("text").setFadeValue(fadeValue);
        }

        m_renderer.glScene().setSplitType(MCGLScene::Single);

        m_menuManager->render();

        break;

    case StateMachine::GameTransitionIn:
    case StateMachine::GameTransitionOut:
    case StateMachine::DoStartlights:
    case StateMachine::Play:

        if (m_stateMachine.isFading())
        {
            Renderer::instance().program("master").setFadeValue(fadeValue);
            Renderer::instance().program("car").setFadeValue(fadeValue);
            Renderer::instance().program("particle").setFadeValue(fadeValue);
            Renderer::instance().program("tile2d").setFadeValue(fadeValue);
            Renderer::instance().program("tile3d").setFadeValue(fadeValue);
            Renderer::instance().program("text").setFadeValue(fadeValue);
        }

        if (m_game.hasTwoHumanPlayers())
        {
            m_renderer.glScene().setSplitType(MCGLScene::Left);
            renderPlayerScene(m_camera[1]);
            m_timingOverlay[1].render();

            m_renderer.glScene().setSplitType(MCGLScene::Right);
            renderPlayerScene(m_camera[0]);
            m_timingOverlay[0].render();
        }
        else
        {
            m_renderer.glScene().setSplitType(MCGLScene::Single);
            renderPlayerScene(m_camera[0]);
            m_timingOverlay[0].render();
        }

        m_renderer.glScene().setSplitType(MCGLScene::Single);
        renderCommonScene();

        break;

    default:
        break;
    };
}

void Scene::renderPlayerScene(MCCamera & camera)
{
    m_activeTrack->render(&camera);
    m_world->render(&camera);
}

void Scene::renderCommonScene()
{
    if (m_race.checkeredFlagEnabled() && !m_game.hasTwoHumanPlayers())
    {
        m_checkeredFlag->render();
    }

    m_startlightsOverlay->render();
    m_messageOverlay->render();
}

Scene::~Scene()
{
    delete m_settings;
    delete m_credits;
    delete m_help;
    delete m_intro;
    delete m_mainMenu;
    delete m_menuManager;
    delete m_startlights;
    delete m_startlightsOverlay;
    delete m_trackSelectionMenu;
    delete m_messageOverlay;
    delete m_particleManager;
}
