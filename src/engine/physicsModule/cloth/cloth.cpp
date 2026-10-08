#include "cloth.h"

void ClothObj::Initialize(int gridSize, float distance, const glm::vec3& position) {
	float k = -1.0f;
	float b = 0.0f;
	clothSize = gridSize;

	verts.clear();
	structural.clear();
	shear.clear();
	bend.clear();

	verts.reserve(gridSize * gridSize);
	for (int x = 0; x < gridSize; ++x) {
		for (int z = 0; z < gridSize; ++z) {
			verts.push_back(std::make_shared<ClothParticle>());
		}
	}
	float halfSize = (float)(gridSize - 1) * 0.5f;

	if (gridSize < 3) {
		gridSize = 3;
	}

	// Create vertices
	for (int x = 0; x < gridSize; ++x) {
		for (int z = 0; z < gridSize; ++z) {
			int i = z * gridSize + x;

			float x_pos = ((float)x + position.x - halfSize) * distance;
			float z_pos = ((float)z + position.z - halfSize) * distance;
			
			verts[i]->SetPosition(glm::vec3(x_pos, position.y, z_pos));
			verts[i]->SetMass(1.0f);
			verts[i]->SetBounce(0.0f);
			verts[i]->SetFriction(0.9f);
		}
	}

	// Create left to right structural springs
	for (int x = 0; x < gridSize; ++x) {
		for (int z = 0; z < gridSize - 1; ++z) {
			int i = z * gridSize + x;
			int j = (z + 1) * gridSize + x;

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			structural.push_back(spring);
		}
	}

	// Create up and down structural springs
	for (int x = 0; x < gridSize - 1; ++x) {
		for (int z = 0; z < gridSize; ++z) {
			int i = z * gridSize + x;
			int j = z * gridSize + (x + 1);

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			structural.push_back(spring);
		}
	}

	// Create left to right shear springs
	for (int x = 0; x < gridSize - 1; ++x) {
		for (int z = 0; z < gridSize - 1; ++z) {
			int i = z * gridSize + x;
			int j = (z + 1) * gridSize + (x + 1);

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			shear.push_back(spring);
		}
	}

	// Create right to left shear springs
	for (int x = 1; x < gridSize; ++x) {
		for (int z = 0; z < gridSize - 1; ++z) {
			int i = z * gridSize + x;
			int j = (z + 1) * gridSize + (x - 1);

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			shear.push_back(spring);
		}
	}
	
	// Create left to right bend springs
	for (int x = 0; x < gridSize; ++x) {
		for (int z = 0; z < gridSize - 2; ++z) {
			int i = z * gridSize + x;
			int j = (z + 2) * gridSize + x;

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			bend.push_back(spring);
		}
	}

	// Create up and down bend springs
	for (int x = 0; x < gridSize - 2; ++x) {
		for (int z = 0; z < gridSize; ++z) {
			int i = z * gridSize + x;
			int j = z * gridSize + (x + 2);

			float rest = Magnitude(verts[i]->GetPosition() - verts[j]->GetPosition());
			auto spring = std::make_shared<Spring>(k, b, rest);
			spring->SetParticles(verts[i], verts[j]);
			bend.push_back(spring);
		}
	}
}

void ClothObj::SetStructuralSprings(float k, float b) {
	for (int i = 0, size = structural.size(); i < size; ++i) {
		structural[i]->SetConstants(k, b);
	}
}

void ClothObj::SetShearSprings(float k, float b) {
	for (int i = 0, size = shear.size(); i < size; ++i) {
		shear[i]->SetConstants(k, b);
	}
}

void ClothObj::SetBendSprings(float k, float b) {
	for (int i = 0, size = bend.size(); i < size; ++i) {
		bend[i]->SetConstants(k, b);
	}
}

void ClothObj::SetParticleMass(float mass) {
	for (int i = 0, size = verts.size(); i < size; ++i) {
		verts[i]->SetMass(mass);
	}
}

void ClothObj::ApplyForces() {
	for (int i = 0, size = verts.size(); i < size; ++i) {
		verts[i]->ApplyForces();
	}
}

void ClothObj::Update(float dt) {
	for (int i = 0, size = verts.size(); i < size; ++i) {
		verts[i]->Update(dt);
	}
}

void ClothObj::SolveConstraints(const std::vector<OBB>& constraints) {
	for (int i = 0, size = verts.size(); i < size; ++i) {
		verts[i]->SolveConstraints(constraints);
	}
}

void ClothObj::ApplySpringForces(float dt) {
	for (int i = 0, size = structural.size(); i < size; ++i) {
		structural[i]->ApplyForce(dt);
	}
	for (int i = 0, size = shear.size(); i < size; ++i) {
		shear[i]->ApplyForce(dt);
	}
	for (int i = 0, size = bend.size(); i < size; ++i) {
		bend[i]->ApplyForce(dt);
	}
}
/*
void Cloth::Render(bool debug) {
	static const float redDiffuse[]{ 200.0f / 255.0f, 0.0f, 0.0f, 0.0f };
	static const float redAmbient[]{ 200.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f, 0.0f };
	static const float zero[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	glColor3f(redDiffuse[0], redDiffuse[1], redDiffuse[2]);
	glLightfv(GL_LIGHT0, GL_AMBIENT, redAmbient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, redDiffuse);
	glLightfv(GL_LIGHT0, GL_SPECULAR, zero);

	if (debug) {
		for (int i = 0, size = verts.size(); i < size; ++i) {
			verts[i]->Render();
		}

		GLboolean status;
		glGetBooleanv(GL_LIGHTING, &status);
		glDisable(GL_LIGHTING);
		
		glColor3f(1.0f, 0.0f, 1.0f);
		for (int i = 0, size = structural.size(); i < size; ++i) {
			if (structural[i].GetP1() == 0 || structural[i].GetP2() == 0) {
				continue;
			}

			Line l(structural[i].GetP1()->GetPosition(), structural[i].GetP2()->GetPosition());
			::Render(l);
		}

		glColor3f(1.0f, 1.0f, 0.0f);
		for (int i = 0, size = shear.size(); i < size; ++i) {
			if (shear[i].GetP1() == 0 || shear[i].GetP2() == 0) {
				continue;
			}

			Line l(shear[i].GetP1()->GetPosition(), shear[i].GetP2()->GetPosition());
			::Render(l);
		}

		glColor3f(0.0f, 1.0f, 1.0f);
		for (int i = 0, size = bend.size(); i < size; ++i) {
			if (bend[i].GetP1() == 0 || bend[i].GetP2() == 0) {
				continue;
			}

			vec3 p1 = bend[i].GetP1()->GetPosition();
			vec3 p2 = bend[i].GetP2()->GetPosition();
			

			Line l(p1, p2);
			::Render(l);
		}

		if (status) {
			glEnable(GL_LIGHTING);
		}
	}
	else {
		for (int x = 0; x < clothSize - 1; ++x) {
			for (int z = 0; z < clothSize - 1; ++z) {
				int tl = z * clothSize + x;
				int bl = (z + 1) * clothSize + x;
				int tr = z * clothSize + (x + 1);
				int br = (z + 1) * clothSize + (x + 1);

				Triangle t1(verts[tl].GetPosition(), verts[br].GetPosition(), verts[bl].GetPosition());
				Triangle t2(verts[tl].GetPosition(), verts[tr].GetPosition(), verts[br].GetPosition());

				::Render(t1, true);
				::Render(t2, true);
			}
		}
	}
}
*/