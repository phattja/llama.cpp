import { API_UPLOADS, SETTINGS_KEYS } from '$lib/constants';
import { settingsStore } from '$lib/stores/settings/index.svelte';
import { apiFetch } from '$lib/utils';

export interface ChatUploadResult {
	id: string;
	mimeType: string;
	name: string;
	path: string;
	size: number;
}

interface UploadResponse {
	id: string;
	mime_type?: string;
	mimeType?: string;
	name: string;
	path: string;
	size: number;
}

/**
 * Uploads a chat attachment to llama-server as a temporary file so MCP
 * tools can open a real path instead of a data URI.
 */
export class ChatUploadsService {
	private static cache = new Map<string, ChatUploadResult>();

	static async upload(name: string, mimeType: string, data: string): Promise<ChatUploadResult> {
		const key = `${name}:${mimeType}:${data.slice(0, 48)}:${data.length}`;
		const cached = this.cache.get(key);

		if (cached) {
			return cached;
		}

		const dir = String(settingsStore.config[SETTINGS_KEYS.ATTACHMENT_SERVER_DIR] ?? '').trim();
		const ttlRaw = Number(settingsStore.config[SETTINGS_KEYS.ATTACHMENT_KEEP_HOURS]);
		const ttl_hours = Number.isFinite(ttlRaw) ? ttlRaw : 24;

		const response = await apiFetch<UploadResponse>(API_UPLOADS.CREATE, {
			body: JSON.stringify({
				data,
				mime_type: mimeType,
				name,
				ttl_hours,
				...(dir ? { dir } : {})
			}),
			method: 'POST'
		});

		const result: ChatUploadResult = {
			id: response.id,
			mimeType: response.mime_type ?? response.mimeType ?? mimeType,
			name: response.name,
			path: response.path,
			size: response.size
		};

		this.cache.set(key, result);

		return result;
	}

	static async listFiles(): Promise<{ name: string; path: string; size: number }[]> {
		const dir = String(settingsStore.config[SETTINGS_KEYS.ATTACHMENT_SERVER_DIR] ?? '').trim();
		const ttlRaw = Number(settingsStore.config[SETTINGS_KEYS.ATTACHMENT_KEEP_HOURS]);
		const ttl_hours = Number.isFinite(ttlRaw) ? ttlRaw : 24;
		const query = new URLSearchParams({ ttl_hours: String(ttl_hours) });

		if (dir) {
			query.set('path', dir);
		}

		const listing = await apiFetch<{ files?: { name: string; path: string; size: number }[] }>(
			`${API_UPLOADS.DIRS}?${query.toString()}`
		);

		return listing.files ?? [];
	}
}
